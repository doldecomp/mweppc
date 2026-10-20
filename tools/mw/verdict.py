"""Whether a compiled function is the original's: the one definition of "exact".

A function is exact when, linked the way the original was, it would be the original's bytes. Concretely, for each
source's object:

1. Every function of the object that has a row is found by name (its C name, whatever the decoration).
2. Each datum of the object that no binding places (a string literal, a jump table, a static, a static function) gets
   ONE address, solved from all the object's functions together: wherever a function that is the original's but for its
   relocated fields refers to it, the original's instruction at that place says where the original has it. A datum is a
   named symbol, or, for a reference through a section symbol, the offset it reaches. A datum the functions disagree
   about is placed where most of them say, and the others are not exact (their code needs it in two places at once); two
   data placed at one address make every function referring to either not exact (the original has one where the object
   has two).
3. With every reference resolved (bindings and rows for named symbols, the placements for the object's own data),
   the function is exact when:
     - its bytes are the original's;
     - its absolute references are exactly where the original's base relocations are;
     - every initialized datum it references (a literal, a jump table, a static's initial value), resolved the same
       way, has the original's bytes at its address;
     - every .bss static it references lands in the original's uninitialized data.
   A reference nothing resolves (an external symbol without a binding) makes it not exact.

Whether the object's data are laid out as in the original (so the unit can link in its place) is a question about
the unit, not its functions: the relink answers it.

Each inexact function gets a reason. similarity() scores how close an inexact function is, for the status tools.
"""

from __future__ import annotations

import bisect
import collections
import concurrent.futures
import difflib
import json
import multiprocessing
import os
import re
import struct
from dataclasses import dataclass

from . import build, config
from .coff import DIR32, DIR32NB, REL32, CoffError, Object, Section, Symbol, function_extent
from .isolated import FORK
from .names import c_name, is_mangled
from .pe import PEFile, original
from .project import ROOT, Error, Version, digest, relative, write_atomic
from .project import version as get_version

# (what an unresolved reference becomes in a body: an address-sized value, which similarity() masks like any address)
UNRESOLVED = 0x00A0A0A0


@dataclass
class Row:
    """A function row as the comparison sees it."""

    name: str
    address: int
    size: int
    source: str | None
    symbol: str  # the name it is looked up by in the object (its C name, or the mangled name)


@dataclass
class Reference:
    """A function's reference to a named symbol, against the original's (references())."""

    row: Row
    symbol: str  # the linker name referenced
    defined: bool  # whether the function's object defines it
    bound: int | None  # the address bindings or rows give it (None: nothing does)
    original: int  # the address the original's instruction at the same place refers to
    plausible: bool  # the function is the original's but for its relocated fields (else only aligned there)


@dataclass
class Verdict:
    address: int
    name: str
    source: str
    exact: bool
    reason: str = ""  # why not exact ("" when exact)
    body: bytes | None = None  # the function's bytes with every resolvable reference resolved (None: not found)
    similarity: float = 0.0  # 1.0 when exact


def rows(version: Version | str | None = None) -> list[Row]:
    """The version's function rows. Two rows of one name (a validation error) are told apart by address: the one with
    a source keeps the name for lookups."""
    v = get_version(version)
    out = [Row(f.name, f.address, f.size, f.source, f.name) for f in config.functions(v)]
    by_name = collections.defaultdict(list)
    for row in out:
        by_name[row.name].append(row)
    for same in by_name.values():
        if len(same) > 1:
            keep = next((r for r in same if r.source), same[0])
            for row in same:
                if row is not keep:
                    row.symbol = f"{row.name}__{row.address:08x}"
    return out


# --------------------------------------------------------------------------------------------- the per-object pass


class _Unit:
    """One object being compared against the original."""

    def __init__(self, pe: PEFile, obj: Object, addresses: dict[str, int], fixups: tuple[int, ...]):
        self.pe, self.obj, self.addresses, self.fixups = pe, obj, addresses, fixups
        self.fixup_set = frozenset(fixups)
        self.code_bases: dict[int, int] = {}  # code section number -> address (from its rows)
        self.places: dict[tuple[int, int], int] = {}  # datum -> address
        self.conflicts: dict[tuple[int, int], set[int]] = {}  # datum -> the other addresses some functions need
        self.shared: dict[tuple[int, int], tuple[int, int]] = {}  # datum -> another datum placed at its address
        self.checked: dict[tuple[int, int], str] = {}
        self.used: dict[tuple[int, int], set[int]] = collections.defaultdict(set)  # datum -> rows reaching it

    # ---- lookups

    def named_address(self, symbol: Symbol) -> int | None:
        """The address bindings or rows give a symbol's name."""
        if symbol.name in self.addresses:
            return self.addresses[symbol.name]
        c = c_name(symbol.name)
        return self.addresses.get(c if is_mangled(c) else "_" + c)

    def find_function(self, row: Row) -> Symbol:
        target = row.symbol
        linker = target if is_mangled(target) else "_" + target
        found = [s for s in self.obj.symbols if s.defined and s.name == linker]
        if not found:
            found = [
                s
                for s in self.obj.symbols
                if s.defined and self.obj.section(s.section).code and c_name(s.name) == target
            ]
        if len(found) != 1:
            raise CoffError("not defined in the object" if not found else "defined more than once in the object")
        return found[0]

    def datum(self, symbol: Symbol, addend: int) -> tuple[tuple[int, int], int]:
        """(the datum SYMBOL + ADDEND reaches, as (section, offset); the offset into it). A reference through a section
        symbol reaches the datum at its addend; one through a named symbol, that symbol's datum (the addend a field)."""
        if symbol.aux and symbol.name == self.obj.section(symbol.section).name:
            return (symbol.section, (symbol.value + addend) & 0xFFFFFFFF), 0
        return (symbol.section, symbol.value), addend

    # ---- placement

    def target_of(self, row: Row, start: int, offset: int, kind: int) -> int | None:
        """The address the original's instruction field at OFFSET of ROW's code refers to."""
        site = row.address + offset - start
        word = self.pe.word(site)
        if kind == DIR32:
            return word
        if kind == DIR32NB:
            return (word + self.pe.image_base) & 0xFFFFFFFF
        if kind == REL32:
            return (word + site + 4) & 0xFFFFFFFF
        return None

    def plausible(self, row: Row, section: Section, start: int, end: int) -> bool:
        """Whether a function is the original's but for its relocated fields (only such functions place data)."""
        if end - start != row.size:
            return False
        mine, theirs = bytearray(section.data[start:end]), bytearray(self.pe.read(row.address, row.size))
        for r in section.relocations:
            if start <= r.offset < end:
                mine[r.offset - start : r.offset - start + 4] = theirs[r.offset - start : r.offset - start + 4]
        return mine == theirs

    def own_data(self, section: Section, start: int, end: int):
        """(relocation, symbol, addend) of each reference in [START, END) to a datum of the object no name places."""
        for r in section.relocations:
            if not start <= r.offset < end:
                continue
            target = self.obj.symbol(r.symbol)
            if not target.defined or self.named_address(target) is not None:
                continue
            if section.code and target.section == section.number:
                continue  # (the function's own labels)
            if self.obj.section(target.section).code and target.section in self.code_bases:
                continue
            yield r, target, struct.unpack_from("<I", section.data, r.offset)[0]

    def solve(self, functions: list[tuple[Row, Symbol, Section, int, int]]) -> None:
        """Place the object's data from every candidate function's references (the module's step 2)."""
        for row, _, section, start, _ in functions:
            self.code_bases.setdefault(section.number, row.address - start)
        votes: dict[tuple[int, int], collections.Counter] = collections.defaultdict(collections.Counter)
        for row, _, section, start, end in functions:
            if not self.plausible(row, section, start, end):
                continue
            seen = collections.defaultdict(set)
            for r, target, addend in self.own_data(section, start, end):
                key, delta = self.datum(target, addend)
                there = self.target_of(row, start, r.offset, r.kind)
                if there is not None:
                    seen[key].add((there - delta) & 0xFFFFFFFF)
            for key, places in seen.items():
                for place in places:
                    votes[key][place] += 1
        for key, counter in votes.items():
            (place, _), *others = counter.most_common()
            self.places[key] = place
            if others:
                self.conflicts[key] = {p for p, _ in others}
        # (data the functions reach only through other data, such as the strings of a table of pointers: where the
        # original's word in the placed datum points)
        pending = list(self.places)
        while pending:
            key = pending.pop()
            section = self.obj.section(key[0])
            if section.code or section.bss:
                continue
            end = self.datum_end(section, key[1])
            for r, target, addend in self.own_data(section, key[1], end):
                inner, delta = self.datum(target, addend)
                if inner in self.places or r.kind != DIR32 or r.offset + 4 > end:
                    continue
                try:
                    word = self.pe.word(self.places[key] + r.offset - key[1])
                except (ValueError, struct.error):
                    continue
                self.places[inner] = (word - delta) & 0xFFFFFFFF
                pending.append(inner)
        at: dict[int, tuple[int, int]] = {}
        for key, place in sorted(self.places.items()):
            if place in at:
                self.shared[key] = at[place]
                self.shared.setdefault(at[place], key)
            at.setdefault(place, key)

    # ---- resolution

    def resolve(self, symbol: Symbol, addend: int, own: tuple[Section, int, int] | None = None) -> int | None:
        """The address SYMBOL + ADDEND refers to (None: nothing resolves it). OWN: (section, start, address) of the
        function being resolved, whose labels resolve within it."""
        address = self.named_address(symbol)
        if address is not None:
            return (address + addend) & 0xFFFFFFFF
        if symbol.section == -1:
            return (symbol.value + addend) & 0xFFFFFFFF
        if not symbol.defined:
            return None
        if own and symbol.section == own[0].number:
            return (own[2] - own[1] + symbol.value + addend) & 0xFFFFFFFF
        if symbol.section in self.code_bases:
            return (self.code_bases[symbol.section] + symbol.value + addend) & 0xFFFFFFFF
        key, delta = self.datum(symbol, addend)
        place = self.places.get(key)
        return None if place is None else (place + delta) & 0xFFFFFFFF

    def datum_end(self, section: Section, offset: int) -> int:
        """Where the datum at OFFSET of a data section ends: at the next named symbol of the section, or its end."""
        ends = [
            s.value
            for s in self.obj.symbols
            if s.section == section.number and s.value > offset and not s.name.startswith(".")
        ]
        return min(ends, default=section.size)

    def check_datum(self, key: tuple[int, int], name: str) -> str:
        """ "" when datum KEY (placed) is the original's: initialized data with the original's bytes there (its own
        references resolved), uninitialized data in the original's .bss."""
        if key in self.checked:
            return self.checked[key]
        self.checked[key] = ""
        section, offset = self.obj.section(key[0]), key[1]
        place = self.places[key]
        reason = ""
        if section.code:
            pass
        elif section.bss:
            try:
                if not self.pe.section_for_address(place).bss:
                    reason = f"{name}: placed at 0x{place:08x}, outside .bss"
            except ValueError:
                reason = f"{name}: placed at 0x{place:08x}, outside the executable"
        else:
            end = self.datum_end(section, offset)
            data = bytearray(section.data[offset:end])
            for r in section.relocations:
                if not offset <= r.offset < end:
                    continue
                if r.offset + 4 > end:
                    reason = f"{name}: a reference crosses the datum's end"
                    break
                target = self.obj.symbol(r.symbol)
                local = r.offset - offset
                value = self.resolve(target, struct.unpack_from("<I", data, local)[0])
                if value is None:
                    reason = f"{name}: refers to {target.name}, which nothing places"
                    break
                if r.kind == DIR32NB:
                    value -= self.pe.image_base
                elif r.kind != DIR32:
                    reason = f"{name}: relocation kind {r.kind} in data"
                    break
                struct.pack_into("<I", data, local, value & 0xFFFFFFFF)
            if not reason:
                try:
                    same = self.pe.read(place, len(data)) == bytes(data)
                except ValueError:
                    same = False
                if not same:
                    reason = f"{name}: its bytes differ from the original's at 0x{place:08x}"
        self.checked[key] = reason
        return reason

    def verdict(self, row: Row, symbol: Symbol, section: Section, start: int, end: int) -> Verdict:
        body = bytearray(section.data[start:end])
        reasons, absolute = [], set()
        same_size = end - start == row.size
        for r in section.relocations:
            if not start <= r.offset < end:
                continue
            local = r.offset - start
            if local + 4 > len(body):
                reasons.append("a relocation crosses the function's end")
                continue
            target = self.obj.symbol(r.symbol)
            addend = struct.unpack_from("<I", body, local)[0]
            address = self.resolve(target, addend, (section, start, row.address))
            if address is None:
                reasons.append(f"{target.name} is not bound" if not target.defined else f"{target.name} is not placed")
                address = UNRESOLVED
            elif (
                target.defined
                and self.named_address(target) is None
                and target.section != section.number
                and target.section not in self.code_bases
            ):
                key, delta = self.datum(target, addend)
                self.used[key].add(row.address)
                if same_size and key in self.conflicts:
                    there = self.target_of(row, start, r.offset, r.kind)
                    if there is not None and (there - delta) & 0xFFFFFFFF != self.places[key]:
                        reasons.append(
                            f"{target.name}: needs it at 0x{(there - delta) & 0xFFFFFFFF:08x}, the "
                            f"object's other functions at 0x{self.places[key]:08x}"
                        )
                if key in self.shared:
                    reasons.append(
                        f"{target.name}: placed at 0x{self.places[key]:08x} with another datum of the object"
                    )
                problem = self.check_datum(key, target.name)
                if problem:
                    reasons.append(problem)
            if r.kind == DIR32:
                value = address
                absolute.add(row.address + local)
            elif r.kind == DIR32NB:
                value = address - self.pe.image_base
            elif r.kind == REL32:
                value = address - (row.address + local + 4)
            else:
                reasons.append(f"relocation kind {r.kind}")
                continue
            struct.pack_into("<I", body, local, value & 0xFFFFFFFF)
        body = bytes(body)
        if not same_size:
            reasons.insert(0, f"{len(body)} bytes, the original's {row.size}")
        elif body != self.pe.read(row.address, row.size):
            reasons.insert(0, "code differs")
        else:
            lo = bisect.bisect_left(self.fixups, row.address)
            hi = bisect.bisect_left(self.fixups, row.address + row.size)
            if absolute != set(self.fixups[lo:hi]):
                reasons.append("absolute references are not where the original's relocations are")
        return Verdict(
            row.address,
            row.name,
            row.source,
            not reasons,
            "; ".join(dict.fromkeys(reasons)),
            body,
            1.0 if not reasons else 0.0,
        )

    def references(self, row: Row, section: Section, start: int, end: int, aligned: bool) -> list[Reference]:
        """Each reference of the function to a named symbol (bound, external, or a named datum or function of the
        object), with where the original's instruction at that place refers. Only a function that is the original's but
        for its relocated fields says so, unless ALIGNED: then also each reference inside a stretch of code the function
        and the original's have in common (evidence, not exactness)."""
        plausible = self.plausible(row, section, start, end)
        if plausible:
            where = {r.offset - start: r.offset - start for r in section.relocations if start <= r.offset < end}
        elif aligned:
            where = _aligned_sites(
                section,
                start,
                end,
                self.pe.read(row.address, row.size),
                [f - row.address for f in self.fixups if row.address <= f < row.address + row.size],
            )
        else:
            return []
        fixups = self.fixup_set
        out = []
        for r in section.relocations:
            if not start <= r.offset < end or r.offset - start not in where:
                continue
            target = self.obj.symbol(r.symbol)
            bound = self.named_address(target)
            if (
                target.defined
                and bound is None
                and (
                    target.name.startswith(("@", "_@", "."))
                    or target.section == section.number
                    or target.aux
                    and target.name == self.obj.section(target.section).name
                )
            ):
                continue  # (the object's literals, sections and the function's own labels: unnamed)
            there = where[r.offset - start]
            site = row.address + there
            if there + 4 > row.size or (r.kind == DIR32 and site not in fixups):
                continue
            word = self.pe.word(site)
            addend = struct.unpack_from("<I", section.data, r.offset)[0]
            if r.kind == DIR32:
                value = word
            elif r.kind == REL32:
                value = word + site + 4
            elif r.kind == DIR32NB:
                value = word + self.pe.image_base
            else:
                continue
            out.append(Reference(row, target.name, target.defined, bound, (value - addend) & 0xFFFFFFFF, plausible))
        return out


def compare_object(
    pe: PEFile,
    obj: Object,
    unit_rows: list[Row],
    addresses: dict[str, int],
    fixups: tuple[int, ...],
    score: bool = True,
) -> list[Verdict]:
    """The verdicts of UNIT_ROWS' functions in OBJ."""
    unit = _Unit(pe, obj, addresses, fixups)
    found, out = [], []
    for row in unit_rows:
        try:
            symbol = unit.find_function(row)
        except CoffError as e:
            out.append(Verdict(row.address, row.name, row.source, False, str(e)))
            continue
        section, start, end = function_extent(obj, symbol)
        found.append((row, symbol, section, start, end))
    unit.solve(found)
    for row, symbol, section, start, end in found:
        v = unit.verdict(row, symbol, section, start, end)
        if not v.exact and score:
            v.similarity = similarity(v.body, pe.read(row.address, row.size), row.address)
        out.append(v)
    return out


# ------------------------------------------------------------------------------------------------------- similarity

_ADDRESS = re.compile(r"0x[0-9a-f]{6,}")


def references_agree(left: str, right: str, lo: int, hi: int) -> bool:
    """Whether two aligned instructions' operands refer to the same addresses. A branch inside the function
    ([lo, hi)) agrees with any other (code of another length moves them), and so does an unresolved reference
    (UNRESOLVED: a datum not placed or a name not bound, which the verdict reports on its own)."""
    xs, ys = [int(m, 16) for m in _ADDRESS.findall(left)], [int(m, 16) for m in _ADDRESS.findall(right)]
    if len(xs) != len(ys):
        return False
    return all(x == y or UNRESOLVED in (x, y) or (lo <= x < hi and lo <= y < hi) for x, y in zip(xs, ys))


def similarity(body: bytes, target: bytes, address: int = 0) -> float:
    """How alike two functions' code is, 0..1: their instructions matched by difflib with absolute addresses masked,
    then a matched pair whose resolved references differ (another global, another callee) not counted."""
    import capstone

    md = getattr(similarity, "md", None)
    if md is None:
        md = similarity.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)

    def words(code):
        return [(f"{i.mnemonic} {_ADDRESS.sub('A', i.op_str)}", i.op_str) for i in md.disasm(code, address)]

    a, b = words(body), words(target)
    if not a and not b:
        return 1.0
    lo, hi = address, address + max(len(body), len(target))
    matcher = difflib.SequenceMatcher(None, [w for w, _ in a], [w for w, _ in b], autojunk=False)
    same = sum(
        references_agree(a[i + k][1], b[j + k][1], lo, hi)
        for i, j, n in matcher.get_matching_blocks()
        for k in range(n)
    )
    return 2 * same / (len(a) + len(b))


# ------------------------------------------------------------------------------------------------------ whole version


def addresses(version: Version | str | None, table_rows: list[Row]) -> dict[str, int]:
    """{linker name: address}: bindings, then each row's own name (which wins)."""
    out = dict(config.bindings(version))
    for row in table_rows:
        out[row.symbol if is_mangled(row.symbol) else "_" + row.symbol] = row.address
    return out


_context = None


def _compare_source(job):
    version, source, source_rows = job
    pe, addrs = original(version), _context
    path = build.object_path(version, source)
    try:
        obj = Object.read(path)
    except (FileNotFoundError, CoffError) as e:
        return source, [Verdict(r.address, r.name, source, False, f"no object: {e}") for r in source_rows]
    return source, compare_object(pe, obj, source_rows, addrs, pe.base_relocations)


def _by_source(version: Version, sources: list[str] | None) -> dict[str, list[Row]]:
    """{source: its rows} (only SOURCES'), with the workers' context (the addresses) set and the original loaded, so
    that forked workers inherit both."""
    global _context
    table = rows(version)
    _context = addresses(version, table)
    original(version)
    by_source = collections.defaultdict(list)
    for row in table:
        if row.source and (sources is None or row.source in sources):
            by_source[row.source].append(row)
    return by_source


def _map(function, work: list, jobs: int | None = None) -> list:
    """FUNCTION of each of WORK, in order: in forked processes, JOBS or $JOBS at once (else one per core); in this
    process where processes cannot fork."""
    jobs = jobs or int(os.environ.get("JOBS") or 0) or os.cpu_count() or 1
    if jobs > 1 and len(work) > 1 and FORK:
        with concurrent.futures.ProcessPoolExecutor(jobs, mp_context=multiprocessing.get_context("fork")) as pool:
            return list(pool.map(function, work, chunksize=2))
    return [function(job) for job in work]


def compare(
    version: Version | str | None = None,
    sources: list[str] | None = None,
    jobs: int | None = None,
) -> dict[int, Verdict]:
    """{row address: verdict} for every row with a source (or only SOURCES' rows), from the build's objects."""
    v = get_version(version)
    work = [(v.name, s, r) for s, r in _by_source(v, sources).items()]
    results: dict[int, Verdict] = {}
    for _, part in _map(_compare_source, work, jobs):
        results.update((x.address, x) for x in part)
    return results


def _aligned_sites(section: Section, start: int, end: int, original_code: bytes, fixups: list[int]) -> dict[int, int]:
    """{offset in the compiled function: offset in the original} of each relocated field of the function that lies,
    whole, inside a stretch of bytes the two have in common, with relocated fields and call/jmp operands masked."""
    a = bytearray(section.data[start:end])
    sites = [r.offset - start for r in section.relocations if start <= r.offset < end]
    for offset in sites:
        a[offset : offset + 4] = bytes(4)
    b = bytearray(original_code)
    for offset in fixups:
        b[offset : offset + 4] = bytes(4)
    i = 0
    while i < len(b) - 4:
        if b[i] in (0xE8, 0xE9):
            b[i + 1 : i + 5] = bytes(4)
            i += 5
        else:
            i += 1
    out = {}
    if len(a) * len(b) > 4_000_000:
        # (too long for difflib: a field with the same masked bytes around it at the same offset)
        if len(a) == len(b):
            out = {o: o for o in sites if a[max(o - 2, 0) : o + 4] == b[max(o - 2, 0) : o + 4]}
        return out
    for block in difflib.SequenceMatcher(None, bytes(a), bytes(b), autojunk=False).get_matching_blocks():
        for offset in sites:
            if block.a <= offset and offset + 4 <= block.a + block.size:
                out[offset] = block.b + offset - block.a
    return out


def _references_of_source(job):
    version, source, source_rows, aligned = job
    pe = original(version)
    try:
        obj = Object.read(build.object_path(version, source))
    except (FileNotFoundError, CoffError):
        return []
    unit = _Unit(pe, obj, _context, pe.base_relocations)
    found = []
    for row in source_rows:
        try:
            symbol = unit.find_function(row)
        except CoffError:
            continue
        found.append((row, *function_extent(obj, symbol)))
    unit.solve([(row, None, section, start, end) for row, section, start, end in found])
    return [ref for row, section, start, end in found for ref in unit.references(row, section, start, end, aligned)]


def references(
    version: Version | str | None = None, sources: list[str] | None = None, aligned: bool = False
) -> list[Reference]:
    """Every reference to a named symbol of the rows' functions (only SOURCES' with it), from the build's objects:
    _Unit.references()."""
    v = get_version(version)
    work = [(v.name, s, r, aligned) for s, r in _by_source(v, sources).items()]
    return [ref for part in _map(_references_of_source, work) for ref in part]


# ------------------------------------------------------------------------------------------ build/VERSION/compare.json


def inputs(version: Version | str | None = None) -> dict[str, str]:
    """What a comparison depends on, by digest: the configuration it reads and every source's object."""
    v = get_version(version)
    paths = [
        ROOT / "config/sources.json",
        v.config / "config.json",
        v.config / "functions.json",
        v.config / "bindings.json",
    ]
    out = {relative(p): digest(p.read_bytes()) for p in paths}
    for source in config.sources():
        path = build.object_path(v, source)
        out[relative(path)] = digest(path.read_bytes()) if path.exists() else "missing"
    return out


def result_path(version: Version | str | None = None):
    return get_version(version).build / "compare.json"


def save(version: Version | str | None, results: dict[int, Verdict]) -> None:
    functions = {
        f"0x{a:08x}": dict(
            name=v.name, source=v.source, exact=v.exact, reason=v.reason, similarity=round(v.similarity, 4)
        )
        for a, v in sorted(results.items())
    }
    write_atomic(result_path(version), json.dumps(dict(inputs=inputs(version), functions=functions), indent=1) + "\n")


def _current(version: Version | str | None) -> dict:
    """compare.json, refused when anything it depends on has changed since it was written."""
    path = result_path(version)
    if not path.exists():
        raise Error(f"{relative(path)} does not exist: run ninja (configure.py --version {get_version(version)})")
    data = json.loads(path.read_text())
    now = inputs(version)
    changed = sorted(k for k in set(now) | set(data["inputs"]) if now.get(k) != data["inputs"].get(k))
    if changed:
        raise Error(
            f"{relative(path)} is out of date ({', '.join(changed[:3])}{' ...' if len(changed) > 3 else ''} "
            f"changed since): run ninja"
        )
    return data


def load(version: Version | str | None = None) -> dict[int, Verdict]:
    """The last comparison's verdicts, refused when anything it depends on has changed since."""
    return {
        int(a, 16): Verdict(int(a, 16), f["name"], f["source"], f["exact"], f["reason"], None, f["similarity"])
        for a, f in _current(version)["functions"].items()
    }


def compare_file(version: Version | str | None, source: str, path, score: bool = True) -> list[Verdict]:
    """The verdicts of SOURCE's functions, compiled into the object at PATH (one built outside the build); without
    SCORE, the inexact ones' similarity is left 0."""
    v = get_version(version)
    table = rows(v)
    pe = original(v)
    return compare_object(
        pe, Object.read(path), [r for r in table if r.source == source], addresses(v, table), pe.base_relocations, score
    )


def last(version: Version | str | None = None) -> dict[int, dict]:
    """The last comparison's verdicts as they were written ({} if there is none), current or not."""
    path = result_path(version)
    if not path.exists():
        return {}
    return {int(a, 16): f for a, f in json.loads(path.read_text())["functions"].items()}
