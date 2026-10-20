"""Pairing two builds' functions by their code, without names: the original's with a later compiler's (the Wii
compilers, front end 4.2+, are later builds of the same sources by a later MWCC), whose CodeView names them.

A function's code differs between the two in registers and scheduling but keeps its instructions' kinds, the strings
it references, the imports it calls and its place in the call graph. Each side is a list of Functions (in address
order; the other build's with the module, its object, each came from). The pairing:

  anchors     a string, an import set or a constant set that one function of each binary alone references, when
              their code agrees (similarity >= 0.4)
  propagate   from each pair, its callees, its callers and its neighbours (the next functions in address order on our
              side, in its module on theirs): the mutually best pairs among the unpaired ones, similarity >= 0.5
  order       within a module, between pairs whose order agrees (and past its first and last), the unpaired functions
              aligned in order, similarity >= 0.3: twins (identical code) are paired only this way
  global      the rest: each unpaired function's most similar unpaired function of theirs (candidates by shared
              instruction 4-grams), when mutually best, >= 0.7 and 0.1 ahead of the next; then propagate again

Similarity is difflib's ratio of the two instruction sequences, each instruction its mnemonic and its operands' kinds
(register, stack slot, memory, immediate, string, data, call target), blended with the overlap of the strings,
imports and constants the two reference. The result is deterministic.
"""

from __future__ import annotations

import bisect
import collections
import difflib
import os
import re
import struct
from concurrent.futures import ProcessPoolExecutor
from dataclasses import dataclass, field

import capstone
from capstone import x86

from .pe import PEFile

_PRINTABLE = re.compile(rb"[\x09\x0a\x0d\x20-\x7e]{2,}")


@dataclass
class Function:
    address: int
    size: int
    module: str | None = None
    # (the features, filled by features())
    sequence: list = field(default_factory=list)
    strings: list = field(default_factory=list)
    calls: list = field(default_factory=list)
    imports: list = field(default_factory=list)
    constants: list = field(default_factory=list)
    thunk: str | None = None
    callees: list = field(default_factory=list)
    callers: list = field(default_factory=list)


class _Binary:
    def __init__(self, path, asserts=()):
        # (ASSERTS: the assert functions' addresses, whose calls and the code reaching them only one side may have)
        self.asserts = set(asserts)
        self.pe = PEFile(path)
        self.iat = {
            i.slot: f"{i.dll.lower()}!{i.name if i.name is not None else f'#{i.number}'}" for i in self.pe.imports
        }
        self.text = next(s for s in self.pe.sections if s.name == ".text")
        self.strings = {}
        self.md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        self.md.detail = True

    def string_at(self, address):
        if address not in self.strings:
            found = None
            try:
                if self.pe.section_for_address(address).name != ".text":
                    data = self.pe.read(address, 256)
                    end = data.find(b"\0")
                    if end >= 2 and _PRINTABLE.fullmatch(data[:end]):
                        found = data[:end].decode("latin-1")
            except ValueError:
                pass
            self.strings[address] = found
        return self.strings[address]

    def in_text(self, address):
        return self.text.virtual_address <= address < self.text.virtual_address + self.text.virtual_size

    def is_data(self, address):
        try:
            self.pe.section_for_address(address)
            return True
        except ValueError:
            return False

    def features(self, address, size):
        sequence, strings, calls, imported, constants = [], [], [], [], []
        thunk = None
        code = self.pe.read(address, size)
        if code[:2] == b"\xff\x25":
            thunk = self.iat.get(struct.unpack_from("<I", code, 2)[0])
        instructions = list(self.md.disasm(code, address))
        # (an assert: [cmp/test; jcc;] up to three pushes, the call, the stack fix-up)
        dropped = set()
        for k, ins in enumerate(instructions):
            if (
                ins.mnemonic == "call"
                and ins.operands
                and ins.operands[0].type == x86.X86_OP_IMM
                and ins.operands[0].imm & 0xFFFFFFFF in self.asserts
            ):
                dropped.add(k)
                b = k - 1
                while b >= 0 and k - b <= 3 and instructions[b].mnemonic == "push":
                    dropped.add(b)
                    b -= 1
                if b >= 0 and instructions[b].group(x86.X86_GRP_JUMP) and instructions[b].mnemonic != "jmp":
                    dropped.add(b)
                    if b >= 1 and instructions[b - 1].mnemonic in ("cmp", "test"):
                        dropped.add(b - 1)
                if (
                    k + 1 < len(instructions)
                    and instructions[k + 1].mnemonic == "add"
                    and instructions[k + 1].op_str.startswith("esp")
                ):
                    dropped.add(k + 1)
        for k, ins in enumerate(instructions):
            if k in dropped:
                continue
            kinds = []
            for op in ins.operands:
                if op.type == x86.X86_OP_REG:
                    kinds.append("r")
                elif op.type == x86.X86_OP_IMM:
                    value = op.imm & 0xFFFFFFFF
                    if ins.group(x86.X86_GRP_JUMP) or ins.group(x86.X86_GRP_CALL):
                        if ins.mnemonic == "call":
                            calls.append(value)
                        kinds.append("t")
                    elif self.string_at(value) is not None:
                        strings.append(self.string_at(value))
                        kinds.append("s")
                    elif self.in_text(value):
                        calls.append(value)
                        kinds.append("f")
                    elif self.is_data(value):
                        kinds.append("d")
                    else:
                        if 0x100 <= value < 0xFFFFFF00:
                            constants.append(value)
                        kinds.append("i" if value < 0x100 or value >= 0xFFFFFF00 else "I")
                elif op.type == x86.X86_OP_MEM:
                    disp = op.mem.disp & 0xFFFFFFFF
                    if op.mem.base == 0 and op.mem.index == 0 and disp in self.iat:
                        imported.append(self.iat[disp])
                        kinds.append("x")
                    elif op.mem.base == 0 and self.is_data(disp):
                        text = self.string_at(disp)
                        if text is not None:
                            strings.append(text)
                        kinds.append("g")
                    elif op.mem.base in (x86.X86_REG_ESP, x86.X86_REG_EBP):
                        kinds.append("l")
                    else:
                        kinds.append("m")
            sequence.append(ins.mnemonic + " " + ",".join(kinds))
        return dict(sequence=sequence, strings=strings, calls=calls, imports=imported, constants=constants, thunk=thunk)


_binaries = {}


def _features(job):
    path, address, size, asserts = job
    if path not in _binaries:
        _binaries[path] = _Binary(path, asserts)
    return _binaries[path].features(address, size)


def features(path, functions: list[Function], asserts=()) -> None:
    """Fill FUNCTIONS' features from the executable at PATH (in parallel)."""
    jobs = [(str(path), f.address, f.size, tuple(sorted(asserts))) for f in functions]
    with ProcessPoolExecutor(int(os.environ.get("JOBS") or 0) or os.cpu_count() or 1) as pool:
        for f, found in zip(functions, pool.map(_features, jobs, chunksize=64)):
            for key, value in found.items():
                setattr(f, key, value)


def _grams(sequence):
    return {tuple(sequence[i : i + 4]) for i in range(len(sequence) - 3)}


class Matcher:
    def __init__(self, ours: list[Function], theirs: list[Function]):
        self.g, self.w = ours, theirs
        self.g2w, self.w2g = {}, {}
        self.how = {}
        self.cache = {}
        for side in (self.g, self.w):
            index = {f.address: i for i, f in enumerate(side)}
            for f in side:
                f.callees = list(dict.fromkeys(index[c] for c in f.calls if c in index))
                f.callers = []
            for i, f in enumerate(side):
                for c in f.callees:
                    side[c].callers.append(i)
            # (a call to an import thunk is a call to the import)
            for f in side:
                f.imports = f.imports + [side[index[c]].thunk for c in f.calls if c in index and side[index[c]].thunk]

    def similarity(self, i, j):
        key = (i, j)
        if key in self.cache:
            return self.cache[key]
        a, b = self.g[i], self.w[j]
        x, y = a.sequence, b.sequence
        if not x or not y or not 0.4 <= len(x) / len(y) <= 2.5:
            score = 0.0
        else:
            score = difflib.SequenceMatcher(None, x, y, autojunk=len(x) * len(y) > 4_000_000).ratio()
            parts = []
            for name in ("strings", "imports", "constants"):
                p, q = set(getattr(a, name)), set(getattr(b, name))
                if p or q:
                    parts.append(len(p & q) / len(p | q))
            if a.thunk or b.thunk:
                score = 1.0 if a.thunk == b.thunk else 0.0
            elif parts:
                score = 0.6 * score + 0.4 * sum(parts) / len(parts)
        self.cache[key] = score
        return score

    def pair(self, i, j, how, score):
        self.g2w[i], self.w2g[j] = j, i
        self.how[i] = (how, round(score, 3))

    def anchors(self):
        for name in ("strings", "imports", "constants"):
            for key in ("set", "single"):
                found = []
                if key == "set":
                    keys_g, keys_w = collections.defaultdict(list), collections.defaultdict(list)
                    for i, f in enumerate(self.g):
                        if getattr(f, name) and i not in self.g2w:
                            keys_g[tuple(sorted(set(getattr(f, name))))].append(i)
                    for j, f in enumerate(self.w):
                        if getattr(f, name) and j not in self.w2g:
                            keys_w[tuple(sorted(set(getattr(f, name))))].append(j)
                    for k, gs in keys_g.items():
                        if len(gs) == 1 and len(keys_w.get(k, ())) == 1:
                            found.append((gs[0], keys_w[k][0]))
                else:
                    owners_g, owners_w = collections.defaultdict(set), collections.defaultdict(set)
                    for i, f in enumerate(self.g):
                        for v in getattr(f, name):
                            owners_g[v].add(i)
                    for j, f in enumerate(self.w):
                        for v in getattr(f, name):
                            owners_w[v].add(j)
                    for v, gs in owners_g.items():
                        if len(gs) == 1 and len(owners_w.get(v, ())) == 1:
                            found.append((next(iter(gs)), next(iter(owners_w[v]))))
                floor = 0.4 if name == "strings" else 0.6
                for score, i, j in sorted({(self.similarity(i, j), i, j) for i, j in found}, reverse=True):
                    if score >= floor and i not in self.g2w and j not in self.w2g:
                        self.pair(i, j, f"{name}-{key}", score)

    def mutual(self, left, right, floor, how):
        """Pair the mutually best (similarity >= FLOOR) among unpaired LEFT (ours) and RIGHT (theirs)."""
        left = [i for i in dict.fromkeys(left) if i not in self.g2w]
        right = [j for j in dict.fromkeys(right) if j not in self.w2g]
        if not left or not right or len(left) * len(right) > 400:
            return 0
        made = 0
        for score, i, j in sorted(((self.similarity(i, j), i, j) for i in left for j in right), reverse=True):
            if score < floor:
                break
            if i in self.g2w or j in self.w2g:
                continue
            # (mutually best among the still unpaired, and not tied: twins are left to fill_gaps' order)
            row_i = sorted(((self.similarity(i, k), k) for k in right if k not in self.w2g), reverse=True)
            row_j = sorted(((self.similarity(k, j), k) for k in left if k not in self.g2w), reverse=True)
            tied = (
                len(row_i) > 1
                and row_i[0][0] - row_i[1][0] < 0.01
                or len(row_j) > 1
                and row_j[0][0] - row_j[1][0] < 0.01
            )
            if row_i[0][1] == j and row_j[0][1] == i and not tied:
                self.pair(i, j, how, score)
                made += 1
        return made

    def _modules(self):
        order = collections.defaultdict(list)
        for j, f in enumerate(self.w):
            order[f.module].append(j)
        return order

    def propagate(self):
        module_order = self._modules()
        place = {j: (module, k) for module, js in module_order.items() for k, j in enumerate(js)}
        changed = True
        while changed:
            changed = False
            for i, j in list(self.g2w.items()):
                a, b = self.g[i], self.w[j]
                changed |= bool(self.mutual(a.callees, b.callees, 0.5, "callee"))
                changed |= bool(self.mutual(a.callers, b.callers, 0.5, "caller"))
                module, k = place[j]
                js = module_order[module]
                for reach in (1, 2, 3):
                    near_g = [x for x in (i - reach, i + reach) if 0 <= x < len(self.g)]
                    near_w = [js[x] for x in (k - reach, k + reach) if 0 <= x < len(js)]
                    changed |= bool(self.mutual(near_g, near_w, 0.5, "neighbour"))

    def align(self, left, right, floor, how):
        """Pair LEFT (ours, in order) with RIGHT (theirs, in the matching order) monotonically, maximizing the sum of
        the similarities >= FLOOR."""
        n, m = len(left), len(right)
        if not n or not m or n * m > 2500:
            return 0
        score = [[0.0] * (m + 1) for _ in range(n + 1)]
        for a in range(1, n + 1):
            for b in range(1, m + 1):
                s = self.similarity(left[a - 1], right[b - 1])
                score[a][b] = max(score[a - 1][b], score[a][b - 1], score[a - 1][b - 1] + s if s >= floor else -1)
        made, a, b = 0, n, m
        while a and b:
            s = self.similarity(left[a - 1], right[b - 1])
            if s >= floor and score[a][b] == score[a - 1][b - 1] + s:
                self.pair(left[a - 1], right[b - 1], how, s)
                made += 1
                a, b = a - 1, b - 1
            elif score[a][b] == score[a - 1][b]:
                a -= 1
            else:
                b -= 1
        return made

    def fill_gaps(self, floor=0.3, reach=40, edge=12):
        """Within each module, between consecutive pairs whose order agrees (a deferred unit's runs backwards on one
        side), align the unpaired functions in order; likewise past the module's first and last pairs."""
        module_order = self._modules()
        place = {j: k for js in module_order.values() for k, j in enumerate(js)}
        made = 0
        by_module = collections.defaultdict(list)
        for i, j in self.g2w.items():
            by_module[self.w[j].module].append((i, place[j]))
        for module, pairs in by_module.items():
            js = module_order[module]
            pairs.sort()
            rising = sum(1 for (_, b), (_, d) in zip(pairs, pairs[1:]) if d > b)
            sign = 1 if 2 * rising >= len(pairs) - 1 else -1
            chain = _longest_chain(pairs, sign)
            for (i1, k1), (i2, k2) in zip(chain, chain[1:]):
                if i2 - i1 > reach or abs(k2 - k1) > reach:
                    continue
                left = [i for i in range(i1 + 1, i2) if i not in self.g2w]
                right = [js[k] for k in range(k1 + sign, k2, sign) if js[k] not in self.w2g]
                made += self.align(left, right, floor, "order")
            if not chain:
                continue
            (i1, k1), (i2, k2) = chain[0], chain[-1]
            left = []
            for i in range(i1 - 1, max(-1, i1 - 1 - edge), -1):
                if i in self.g2w:
                    break
                left.insert(0, i)
            right = []
            for k in range(k1 - sign, k1 - sign * (edge + 1), -sign):
                if not 0 <= k < len(js) or js[k] in self.w2g:
                    break
                right.insert(0, js[k])
            made += self.align(left, right, floor, "order")
            left = []
            for i in range(i2 + 1, min(len(self.g), i2 + 1 + edge)):
                if i in self.g2w:
                    break
                left.append(i)
            right = []
            for k in range(k2 + sign, k2 + sign * (edge + 1), sign):
                if not 0 <= k < len(js) or js[k] in self.w2g:
                    break
                right.append(js[k])
            made += self.align(left, right, floor, "order")
        return made

    def global_pass(self, floor=0.7, margin=0.1):
        index = collections.defaultdict(list)
        for j, f in enumerate(self.w):
            if j not in self.w2g:
                for gram in _grams(f.sequence):
                    index[gram].append(j)
        best_for = {}
        for i, f in enumerate(self.g):
            if i in self.g2w or len(f.sequence) < 6:
                continue
            votes = collections.Counter()
            for gram in sorted(_grams(f.sequence)):
                owners = index.get(gram, ())
                if len(owners) <= 50:
                    for j in owners:
                        votes[j] += 1.0 / len(owners)
            candidates = [j for j, _ in votes.most_common(12)]
            scored = sorted(((self.similarity(i, j), j) for j in candidates), reverse=True)
            if scored and scored[0][0] >= floor and (len(scored) == 1 or scored[0][0] - scored[1][0] >= margin):
                best_for[i] = scored[0]
        claimed = collections.defaultdict(list)
        for i, (score, j) in best_for.items():
            claimed[j].append((score, i))
        for j, claims in claimed.items():
            claims.sort(reverse=True)
            if len(claims) == 1 or claims[0][0] - claims[1][0] >= margin:
                score, i = claims[0]
                self.pair(i, j, "global", score)

    def run(self, log=print):
        self.anchors()
        log(f"anchors: {len(self.g2w)}")
        self.propagate()
        log(f"propagated: {len(self.g2w)}")
        for _ in range(4):
            before = len(self.g2w)
            self.fill_gaps()
            self.propagate()
            self.global_pass()
            self.propagate()
            log(f"global + propagated: {len(self.g2w)}")
            if len(self.g2w) == before:
                break


def _longest_chain(pairs, sign):
    """The longest subsequence of PAIRS [(i, k)] (sorted by i) whose k rise (SIGN 1) or fall (-1)."""
    back, tails, tail_index = {}, [], []
    for n, (_, k) in enumerate(pairs):
        key = sign * k
        at = bisect.bisect_left(tails, key)
        if at == len(tails):
            tails.append(key)
            tail_index.append(n)
        else:
            tails[at] = key
            tail_index[at] = n
        back[n] = tail_index[at - 1] if at else None
    if not tail_index:
        return []
    out, n = [], tail_index[-1]
    while n is not None:
        out.append(pairs[n])
        n = back[n]
    return out[::-1]
