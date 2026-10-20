#!/usr/bin/env python3
"""python tools/source_only.py VERSION: link the compiler made of the sources alone, build/VERSION/source-only/
mwcceppc.exe, with the linker the original was linked with: every source's object (config/sources.json; an inexact
function as it compiles), the linker's runtime objects, src/imports' import libraries and src/driver/mwcceppc.rc's
resources (llvm-rc, build/VERSION/resources.res). Nothing of the original is in it. ninja runs it (ninja
build/VERSION/source-only/mwcceppc.exe); tools/equivalence.py tests that it compiles as the original does. A derived
version (config.json's derived_from, such as GC_2_0p2) takes its base's rows and bindings.

The machine models (src/backend/MachineSimulation*.c) are linked in the original's order, with stand-ins for what the
original has after two of them, which the scheduler reads and writes past their ends (the sources' clang sides say
how): after the MPC7450's unit, MachineSimulation7400.c's data as the original holds it (its function pointers the
original's addresses), which the 7450's instruction_timing is read into; after the 821's, 16 bytes of .bss, the second
half of which are SpillCode.c's variables and the 821's pipeline slot for FPUnit.

A name the sources refer to and nothing defines is an error, naming who refers to it, unless the reference is in a
function the original does not have (its linker dropped it): that function's first instruction becomes ud2, so that it
traps if it runs after all.

python tools/source_only.py where VERSION ADDRESS...: the function of that compiler an address (a crash) is in, from
its link map, and that function in the original.
"""

from __future__ import annotations

import bisect
import collections
import re
import shutil
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, coff, config, formats, link, project
from mw.coff import DIR32, Object
from mw.names import c_name, is_mangled

LICENSING = project.ROOT / "tools/lmgr_stub/lmgr326b.dll"
ALIGN_8 = 0x00400000
UD2 = b"\x0f\x0b"
# (the machine models' units, linked in the original's order; after a unit, the stand-in for what follows it there:
# whose .data, or how many bytes of .bss)
MODELS = "src/backend/MachineSimulation"
STAND_INS = {
    "src/backend/MachineSimulationAltiVec.c": "src/backend/MachineSimulation7400.c",
    "src/backend/MachineSimulation821.c": 16,
}


def output_path(version):
    return project.version(version).build / "source-only/mwcceppc.exe"


class Errors(project.Error):
    pass


class SourceOnly:
    def __init__(self, version):
        self.version = v = project.version(version)
        # (the configuration read: a derived version's base's)
        data = project.version(v.base) if v.base else v
        self.rows = sorted(config.functions(data), key=lambda r: r.address)
        self.bindings = config.bindings(data)
        self.objects = {c.source: Object.read(build.object_path(v, c.source)) for c in build.compilations(v)}
        # (the linker names of __stdcall functions: _name@bytes, from the bindings and the objects)
        self.decorated = {}
        for name in [*self.bindings, *(s.name for obj in self.objects.values() for s in obj.symbols)]:
            if m := re.fullmatch(r"(?:__imp_)?(_[A-Za-z_]\w*)@(\d+)", name):
                self.decorated.setdefault(m[1], f"{m[1]}@{m[2]}")
        self.order_models(formats.read_splits(data.config / "splits.txt"))
        self.row_symbols = {self.symbol_of(r) for r in self.rows}
        self.errors = []

    def symbol_of(self, row):
        """A row's linker name."""
        if is_mangled(row.name):
            return row.name
        return self.decorated.get("_" + row.name, "_" + row.name)

    def order_models(self, splits):
        """self.objects with the machine models in the original's order where the first of them is, and the stand-ins
        (STAND_INS) after them."""
        names = list(self.objects)
        models = [u.name for u in splits.units if u.name.startswith(MODELS) and u.name in self.objects]
        first = min(names.index(name) for name in models)
        rest = [name for name in names if name not in models]
        defined = {s.name for obj in self.objects.values() for s in obj.symbols if s.external and s.defined}
        ordered = {}
        for name in rest[:first]:
            ordered[name] = self.objects[name]
        for name in models:
            ordered[name] = self.objects[name]
            stand_in = STAND_INS.get(name)
            if isinstance(stand_in, str):
                ordered[f"after {name}"] = self.data_as_original(stand_in)
            elif stand_in is not None:
                end = splits.unit(name).range(".bss").end
                obj = Object()
                number = obj.add_section(
                    ".bss", coff.UNINITIALIZED | coff.READ | coff.WRITE | ALIGN_8, b"", stand_in, section_symbol=False
                ).number
                for symbol, address in sorted(self.bindings.items()):
                    if end <= address < end + stand_in and symbol not in defined:
                        # (a variable the sources leave COMMON: here at its place)
                        obj.add_symbol(symbol, address - end, number, 0, coff.EXTERNAL)
                ordered[f"after {name}"] = obj
        for name in rest[first:]:
            ordered[name] = self.objects[name]
        self.objects = ordered

    def data_as_original(self, source):
        """An object of SOURCE's .data as the original holds it, the addresses of its functions the original's."""
        obj = self.objects[source]
        section = next(s for s in obj.sections if s.name == ".data")
        addresses = {self.symbol_of(r): r.address for r in self.rows if r.source == source}
        data = bytearray(section.data)
        for relocation in section.relocations:
            name = obj.by_index[relocation.symbol].name
            if relocation.kind != DIR32 or name not in addresses:
                raise project.Error(f"{source}'s .data refers to {name}, which is no function of its rows")
            value = struct.unpack_from("<I", data, relocation.offset)[0] + addresses[name]
            struct.pack_into("<I", data, relocation.offset, value & 0xFFFFFFFF)
        stand_in = Object()
        stand_in.add_section(".data", section.flags, bytes(data), section_symbol=False)
        return stand_in

    def prepare(self):
        """The sources' objects, ready to link: duplicates resolved, and each function's place recorded."""
        source_of = {self.symbol_of(r): r.source for r in self.rows}
        # (a function or variable two objects define, outside a COMDAT: the copy of the row's source)
        definers = collections.defaultdict(list)
        for source, obj in self.objects.items():
            comdat = comdat_sections(obj)
            for s in obj.symbols:
                if s.external and s.defined and s.section not in comdat:
                    definers[s.name].append(source)
        keep = {}
        for name, sources in definers.items():
            if len(sources) > 1:
                if source_of.get(name) in sources:
                    keep[name] = source_of[name]
                else:
                    self.errors.append(f"{name} is defined by {', '.join(sources)}: which is the original's?")
        # {source: [(section, offset, name, dead)]}: each function's place, and whether the original lacks it
        self.functions = {}
        for source, obj in self.objects.items():
            self.functions[source] = sorted(
                (s.section, s.value, s.name, s.name not in self.row_symbols)
                for s in obj.symbols
                if s.defined and s.function and s.storage in (coff.EXTERNAL, coff.STATIC)
            )
            for s in obj.symbols:
                if s.external and s.defined and keep.get(s.name, source) != source:
                    # (another object's copy is the original's)
                    s.section, s.value = 0, 0
            for number in comdat_sections(obj):
                mark_any(obj, number)

    def run(self):
        v = self.version
        output = output_path(v)
        work = output.parent / "objects"
        self.prepare()
        runtime = [(path, Object.read(project.ROOT / path)) for path in link.RUNTIME_OBJECTS]
        sources = list(self.objects.items())
        thunks = link.defined_imports()
        missing = undefined(sources + runtime)
        missing = {n: w for n, w in missing.items() if not n.startswith("__imp_") and n not in thunks}
        # (references only code the original lacks makes: dropped, and the code made to trap)
        for source, obj in self.objects.items():
            trap(obj, self.functions.get(source, []), missing, self.errors, source, v)
        if self.errors:
            raise Errors("\n".join(self.errors))
        work.mkdir(parents=True, exist_ok=True)
        for old in work.glob("*"):
            old.unlink()
        inputs = []
        for source, obj in sources:
            path = work / (re.sub(r"[^\w.-]", "_", source) + ".obj")
            obj.write(path)
            inputs.append(path)
        inputs += [Path(link.source_import_library(name)) for name in link.IMPORT_LIBRARIES]
        listing = work / "inputs.txt"
        project.write_atomic(listing, "".join(f"{project.relative(p)}\n" for p in inputs))
        result = link.mwld(
            [*link.RUNTIME_OBJECTS, f"@{project.relative(listing)}", f"build/{v}/resources.res"],
            project.relative(output),
            "-map",
            project.relative(output.with_suffix(".map")),
        )
        if result.returncode:
            output.unlink(missing_ok=True)
            raise project.Error(f"the link fails:\n{(result.stdout + result.stderr)[-3000:]}")
        shutil.copy(LICENSING, output.parent / LICENSING.name)
        print(f"{v}: {project.relative(output)}: {len(self.rows)} functions, all from the sources")


def undefined(objects):
    """{name: {who refers to it}} of the names OBJECTS [(who, Object)] refer to and none defines."""
    defined, referenced = set(), collections.defaultdict(set)
    for where, obj in objects:
        for s in obj.symbols:
            if s.external and (s.defined or s.common):
                defined.add(s.name)
            elif s.undefined:
                referenced[s.name].add(where)
    return {name: where for name, where in referenced.items() if name not in defined}


def comdat_sections(obj):
    """The numbers of OBJ's COMDAT sections (whose section symbol's auxiliary record has a selection)."""
    return {
        s.section
        for s in obj.symbols
        if s.storage == coff.STATIC
        and s.defined
        and s.aux
        and s.name == obj.section(s.section).name
        and obj.section(s.section).flags & 0x1000
        and s.aux[14]
    }


def mark_any(obj, number):
    """Let the linker take any one copy of COMDAT section NUMBER (the compiler marks some 'no duplicates')."""
    for s in obj.symbols:
        if s.storage == coff.STATIC and s.section == number and s.aux and s.aux[14] == 1:
            s.aux = s.aux[:14] + bytes([2]) + s.aux[15:]


def trap(obj, functions, missing, errors, source, version):
    """Drop OBJ's references to the MISSING names where they are in a function the original lacks (FUNCTIONS:
    [(section, offset, name, dead)], sorted), and make such a function trap; any other reference to them is an
    error."""
    trapped = set()
    for section in obj.sections:
        kept = []
        for r in section.relocations:
            symbol = obj.symbol(r.symbol)
            name = symbol.name
            if not symbol.undefined or name not in missing:
                kept.append(r)
                continue
            i = bisect.bisect_right(functions, (section.number, r.offset, "\U0010ffff", True)) - 1
            owner = functions[i] if i >= 0 and functions[i][0] == section.number else None
            if owner is not None and owner[3]:
                trapped.add(owner[:2])
                continue
            errors.append(
                f"{source}: {owner[2] if owner else section.name} refers to {name}, which nothing defines "
                f"(define it in the sources)"
            )
            kept.append(r)
        section.relocations = kept
    for number, value in trapped:
        section = obj.section(number)
        section.data = section.data[:value] + UD2 + section.data[value + 2 :]


def where(version, addresses):
    v = project.version(version)
    path = output_path(v).with_suffix(".map")
    if not path.exists():
        raise project.Error(f"{project.relative(path)} does not exist: ninja {project.relative(output_path(v))}")
    # (the map's symbol table: address, object, symbol)
    entries = []
    for line in path.read_text(errors="replace").splitlines():
        if m := re.fullmatch(r"([0-9a-fA-F]{8}) (\S+\.obj)\s+(\S+)", line.strip()):
            entries.append((int(m[1], 16), m[2], m[3]))
    entries.sort()
    rows = {r.name: r for r in config.functions(project.version(v.base) if v.base else v)}
    for text in addresses:
        try:
            address = int(text, 16)
        except ValueError:
            raise project.Error(f"{text!r} is not a hexadecimal address") from None
        i = bisect.bisect_right(entries, (address, "\U0010ffff")) - 1
        if i < 0:
            print(f"0x{address:08x}: before every symbol")
            continue
        start, module, symbol = entries[i]
        row = rows.get(c_name(symbol))
        place = f"; the original's 0x{row.address + address - start:08x}, {row.source or 'no source'}" if row else ""
        print(f"0x{address:08x}: {module} {symbol}+0x{address - start:x}{place}")


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    args = sys.argv[1:]
    try:
        if args[:1] == ["where"] and len(args) >= 3:
            where(args[1], args[2:])
            return
        if len(args) != 1:
            sys.exit(__doc__)
        SourceOnly(args[0]).run()
    except Errors as e:
        sys.exit(f"error: {args[0]}: the source-only compiler cannot be linked:\n{e}")
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
