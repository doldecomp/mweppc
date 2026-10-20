"""Proving a compiled function equivalent to the original's when their code differs (translation validation).

Both functions are executed symbolically, side by side. The code is cut into segments that end at a conditional
branch, a jump table or a return (unconditional jumps are followed, and so are branches whose condition is decided),
and the two functions' segments are paired from their entries: a pair of program points, one in each, with a relation
saying which of the compiled function's registers and stack slots hold the same values as which of the original's (or
one plus a constant, or one of a few constants). At the end of a paired segment:

- the memory both left (stores and calls, as one expression each) must be the same;
- the branch conditions must be the same, or one the other's negation with the targets swapped; jump tables must be
  indexed by the same value; each pair of successors is a new pair of points, its relation what the two states agree
  on there, intersected over every way in, to a fixpoint (ways in with other values of the function's flags are kept
  apart, a few per pair of addresses: path sensitivity, so a variable set only when a flag is, is read only then);
- at a return: the same return value (as many bits as its type has), the stack balanced, the return address and the
  callee-saved registers the caller's; a call to a function that never returns ends the path like one.

Where the two segments end differently (one tests a range and jumps through a table, the other tests each case), the
pair is reconciled inside the point: a side whose way out the value sets decide runs on alone, else the original's
branch is split (the compiled one's, or a jump table whose index has a few values), each way's condition kept as the
path's (a bound to a few values makes a set, which the path's other conditions filter) and that side run on under it,
until the ends pair or a few steps are taken.

Values are expressions over the inputs (the caller's registers and stack, the arguments, memory at a pair of points),
built in a canonical form (linear sums, sorted commutative operands, compares as one relation, low parts of wider
values, loads forwarded from stores they provably reach, stores to provably different places in one order), so two
computations of one value in other registers, another order or another frame slot are one expression. A store, a call
and a load memory a store may alias are kept in order. A value known to be one of a few constants (a loop counter, a
switch's cases, a flag) carries that set across joins, and two values are the same when they are for each of its
values. What the model does not settle is a difference, never a proof: every failure names where the two first
disagree.

The model of the compiled code (MWCC x86, the compiler's own code): stack arguments, the cdecl convention (eax and
edx returned, ecx clobbered by calls, ebx, esi, edi and ebp kept; a callee's own `ret N` read for __stdcall), no frame
pointer. A char or short argument is a word whose low bits only the callee reads (the callee's prototype says which).
A call's arguments are the words pushed below the frame, at least as many as the callee's parameters (for a call
through a pointer, as the most any such call of the source passes); an indirect callee is cdecl, and a call through
the import table (Win32's __stdcall functions) is not covered. A jump table's index takes only its table's entries:
the two tables must have as many, and an index known to be past them is not covered.
Stack slots whose address is never taken are values, like registers, so the two functions may keep them anywhere. An
address-taken local is an object in memory: one from each function paired by where their code takes it (the alignment
of the two codes), reached at the same offsets in both, and taken to extend to the next one or to the size of the
largest local the source takes the address of, which only makes the proof stricter. The frame's contents before the
function writes them are the caller's, the same for both, so a local read before it is set is one value where both
keep it at one place; prove(uninitialized=True) also takes as one the uninitialized locals the two keep at other places
(paired by the aligned code) and names those the proof used: an assumption that they are not read unset. String
literals and other compiler-generated constants (`@N`) are read as their bytes. The model has no types: where only
they show two accesses apart (two fields of one struct type), prove(disjoint=...) takes a named pair of the compiled
code's instructions as never overlapping, and names it where the proof uses it.
"""

from __future__ import annotations

import collections
import difflib
import heapq
import struct
import time
import zlib
from dataclasses import dataclass, field

import capstone
from capstone import x86

from .coff import DIR32, DIR32NB, REL32, Object, function_extent
from .names import c_name
from .pe import PEFile


class Unsupported(Exception):
    """Code the model does not cover."""


class Differs(Exception):
    """The two functions differ (as far as the model can tell)."""


# ------------------------------------------------------------------------------------------------------ expressions


class E:
    """An expression: an operation, a width in bits and arguments (expressions or plain values). Built only through
    mk(), which interns them: two expressions are the same value exactly when they are the same object."""

    __slots__ = ("args", "key", "local", "op", "w")

    def __repr__(self):
        return self.render(400)

    def render(self, budget: int) -> str:
        """At most about BUDGET characters of the expression (the parts past it as "..."): written out as a tree, a
        shared expression would be exponential in its size."""
        out = f"{self.op}{self.w}("
        for n, a in enumerate(self.args):
            if len(out) >= budget:
                out += "..."
                break
            part = a.render(budget - len(out)) if isinstance(a, E) else hex(a) if isinstance(a, int) else repr(a)
            out += (", " if n else "") + part
        return out + ")"


_interned: dict[tuple, E] = {}
# (leaves that stand for something known only at one pair of points: a state at a segment's start, a value the model
# cannot name; expressions over them are not carried across segments as known values)
_LOCAL = {"in", "undef", "mem0"}


def _stable(a) -> int:
    """A string's or number's part of a key, the same in every run (Python's own string hashes are not)."""
    return zlib.crc32(a.encode()) if isinstance(a, str) else a


def mk(op: str, w: int, *args) -> E:
    # (interned by the operation and its arguments, which are interned themselves; KEY orders commutative operands:
    # a hash of the expression's content, the same in every run)
    found = _interned.get((op, w, args))
    if found is not None:
        return found
    e = E()
    e.op, e.w, e.args = op, w, args
    e.key = hash((_stable(op), w, tuple(a.key if isinstance(a, E) else _stable(a) for a in args)))
    e.local = op in _LOCAL or any(isinstance(a, E) and a.local for a in args)
    _interned[op, w, args] = e
    return e


def mask(w: int) -> int:
    return (1 << w) - 1


def signed(v: int, w: int) -> int:
    return v - (1 << w) if v >> (w - 1) & 1 else v


def const(w: int, v: int) -> E:
    return mk("c", w, v & mask(w))


def is_const(e: E) -> bool:
    return e.op == "c"


def value(e: E) -> int:
    return e.args[0]


_undefined = [0]


def undef(w: int, why: str) -> E:
    """A value the model cannot name: never equal to anything else."""
    _undefined[0] += 1
    return mk("undef", w, why, _undefined[0])


# ---- linear sums: ("lin", w, constant, coefficient, term, coefficient, term, ...), terms sorted by key


def linear(e: E) -> tuple[dict[E, int], int]:
    if e.op == "lin":
        args = e.args
        return {args[i + 1]: args[i] for i in range(1, len(args), 2)}, args[0]
    if is_const(e):
        return {}, value(e)
    return {e: 1}, 0


def make_linear(w: int, terms: dict[E, int], c: int) -> E:
    m = mask(w)
    terms = {t: k & m for t, k in terms.items() if k & m}
    c &= m
    if not terms:
        return const(w, c)
    if c == 0 and len(terms) == 1:
        ((t, k),) = terms.items()
        if k == 1:
            return t
    flat = []
    for t in sorted(terms, key=lambda t: t.key):
        flat += [terms[t], t]
    return mk("lin", w, c, *flat)


def add(w: int, *xs: E, scale: int = 1) -> E:
    terms: dict[E, int] = collections.defaultdict(int)
    c = 0
    for x in xs:
        t, k = linear(x)
        for term, coefficient in t.items():
            terms[term] += coefficient
        c += k
    return make_linear(w, {t: k * scale for t, k in terms.items()}, c * scale)


def sub(w: int, a: E, b: E) -> E:
    return add(w, a, scale_by(w, b, -1))


def scale_by(w: int, a: E, k: int) -> E:
    t, c = linear(a)
    return make_linear(w, {term: coefficient * k for term, coefficient in t.items()}, c * k)


def mul(w: int, a: E, b: E) -> E:
    if is_const(b):
        return scale_by(w, a, value(b))
    if is_const(a):
        return scale_by(w, b, value(a))
    a, b = sorted((a, b), key=lambda e: e.key)
    return mk("mul", w, a, b)


# ---- bitwise


def bitop(op: str, w: int, *xs: E) -> E:
    m = mask(w)
    flat: list[E] = []
    for x in xs:
        flat += x.args if x.op == op else [x]
    c = {"and": m, "or": 0, "xor": 0}[op]
    rest: list[E] = []
    for x in flat:
        if is_const(x):
            c = c & value(x) if op == "and" else c | value(x) if op == "or" else c ^ value(x)
        else:
            rest.append(x)
    if op == "xor":
        counts = collections.Counter(rest)
        rest = [x for x, n in counts.items() if n % 2]
    else:
        rest = list(dict.fromkeys(rest))
    if op == "and" and c == 0 or op == "or" and c == m:
        return const(w, c)
    if op == "and" and len(rest) == 1 and rest[0].op == "zext" and c != m:
        # (a zero-extended value masked: the mask's part that can be set)
        x = rest[0].args[0]
        return zext(w, bitop("and", x.w, x, const(x.w, c)))
    if op == "and" and c != m:
        # (a mask of the low byte or half: those bits of the operands, zero-extended, the one spelling of x & 0xFF,
        # (UInt8)x and a byte loaded into a cleared register)
        n = next((n for n in (1, 8, 16) if n < w and c < 1 << n), None)
        if n is not None:
            return zext(w, bitop("and", n, *(extract(x, 0, n) for x in rest), const(n, c)))
    if not (op == "and" and c == m or op != "and" and c == 0):
        rest.append(const(w, c))
    if not rest:
        return const(w, c)
    if len(rest) == 1:
        return rest[0]
    return mk(op, w, *sorted(rest, key=lambda e: e.key))


def bitnot(w: int, x: E) -> E:
    if is_const(x):
        return const(w, ~value(x))
    if x.op == "not":
        return x.args[0]
    return mk("not", w, x)


# ---- parts of values


def extract(e: E, lo: int, n: int) -> E:
    """Bits [LO, LO+N) of E."""
    if lo == 0 and n == e.w:
        return e
    if is_const(e):
        return const(n, value(e) >> lo)
    if e.op == "ext":
        return extract(e.args[0], e.args[1] + lo, n)
    if e.op == "ins":
        base, at, x = e.args
        if at <= lo and lo + n <= at + x.w:
            return extract(x, lo - at, n)
        if lo + n <= at or lo >= at + x.w:
            return extract(base, lo, n)
    if e.op in ("zext", "sext"):
        x = e.args[0]
        if lo + n <= x.w:
            return extract(x, lo, n)
        if e.op == "zext" and lo >= x.w:
            return const(n, 0)
    if e.op == "cat":
        at = 0
        for part in e.args:
            if at <= lo and lo + n <= at + part.w:
                return extract(part, lo - at, n)
            at += part.w
    if lo == 0:
        # (the low bits of a sum, a product or a bitwise operation are those of its operands' low bits)
        if e.op == "lin":
            t, c = linear(e)
            return make_linear(n, {extract(term, 0, n): k for term, k in t.items()}, c)
        if e.op in ("and", "or", "xor"):
            return bitop(e.op, n, *(extract(x, 0, n) for x in e.args))
        if e.op == "not":
            return bitnot(n, extract(e.args[0], 0, n))
        if e.op == "mul":
            return mul(n, extract(e.args[0], 0, n), extract(e.args[1], 0, n))
    return mk("ext", n, e, lo)


def insert(base: E, lo: int, x: E) -> E:
    """BASE with bits [LO, LO+width of X) replaced by X."""
    n = x.w
    if lo == 0 and n == base.w:
        return x
    if x.op == "ext" and x.args[0] is base and x.args[1] == lo:
        return base
    if is_const(base) and is_const(x):
        m = mask(n) << lo
        return const(base.w, value(base) & ~m | value(x) << lo)
    if is_const(base) and value(base) == 0 and lo == 0:
        return zext(base.w, x)
    if base.op == "ins" and base.args[1] == lo and base.args[2].w == n:
        return insert(base.args[0], lo, x)
    return mk("ins", base.w, base, lo, x)


def zext(w: int, x: E) -> E:
    if x.w == w:
        return x
    if is_const(x):
        return const(w, value(x))
    if x.op == "zext":
        return zext(w, x.args[0])
    return mk("zext", w, x)


def sext(w: int, x: E) -> E:
    if x.w == w:
        return x
    if is_const(x):
        return const(w, signed(value(x), x.w))
    if x.op == "sext":
        return sext(w, x.args[0])
    return mk("sext", w, x)


def cat(parts: list[E]) -> E:
    """The parts' bits, the first lowest."""
    merged: list[E] = []
    for part in parts:
        if merged and part.op == "ext" and merged[-1].op == "ext":
            last = merged[-1]
            if last.args[0] is part.args[0] and last.args[1] + last.w == part.args[1]:
                merged[-1] = extract(last.args[0], last.args[1], last.w + part.w)
                continue
        merged.append(part)
    if len(merged) == 1:
        return merged[0]
    return mk("cat", sum(p.w for p in merged), *merged)


def shift(op: str, w: int, x: E, n: E) -> E:
    if is_const(n):
        k = value(n) & 31
        if k == 0:
            return x
        if op == "shl":
            return scale_by(w, x, 1 << k)
        if is_const(x):
            v = value(x) >> k if op == "shr" else signed(value(x), w) >> k
            return const(w, v)
        return mk(op, w, x, k)
    return mk(op + "v", w, x, n)


# ---- conditions: ("eq"|"ne", 1, d) for d == 0 / != 0; ("lt"|"le"|"ltu"|"leu", 1, a, b); constants true/false

TRUE, FALSE = const(1, 1), const(1, 0)


def _difference(w: int, a: E, b: E) -> E:
    d = sub(w, a, b)
    # (d == 0 and -d == 0 are one condition: the sign that makes the first term's coefficient the smaller)
    t, _ = linear(d)
    if t:
        first = min(t, key=lambda e: e.key)
        if t[first] & mask(w) > (-t[first]) & mask(w):
            d = scale_by(w, d, -1)
    return d


def equal(a: E, b: E) -> E:
    d = _difference(a.w, a, b)
    if is_const(d):
        return TRUE if value(d) == 0 else FALSE
    t, c = linear(d)
    if len(t) == 1:
        # (an extended value equal to a constant is the narrower value equal to it, when it fits: extending is one
        # to one)
        ((x, k),) = t.items()
        if x.op in ("sext", "zext") and k in (1, mask(d.w)):
            n = x.args[0].w
            target = (-c if k == 1 else c) & mask(d.w)
            fits = signed(target, d.w) == signed(target & mask(n), n) if x.op == "sext" else target < 1 << n
            if not fits:
                return FALSE
            return equal(x.args[0], const(n, target))
    return mk("eq", 1, d)


def relation(kind: str, a: E, b: E) -> E:
    """a KIND b, KIND one of eq ne lt le gt ge (signed) ltu leu gtu geu."""
    w = a.w
    if kind == "eq":
        return equal(a, b)
    if kind == "ne":
        return negate(equal(a, b))
    if kind in ("gt", "ge", "gtu", "geu"):
        return relation({"gt": "lt", "ge": "le", "gtu": "ltu", "geu": "leu"}[kind], b, a)
    unsigned = kind.endswith("u")
    if is_const(a) and is_const(b):
        x, y = (value(a), value(b)) if unsigned else (signed(value(a), w), signed(value(b), w))
        return TRUE if (x < y if kind.startswith("lt") else x <= y) else FALSE
    lowest, highest = (0, mask(w)) if unsigned else (-(1 << (w - 1)), (1 << (w - 1)) - 1)
    # (a < c is a <= c-1, c < b is c+1 <= b: one spelling for each)
    if kind.startswith("lt"):
        if is_const(b):
            v = value(b) if unsigned else signed(value(b), w)
            return FALSE if v == lowest else relation("leu" if unsigned else "le", a, const(w, v - 1))
        if is_const(a):
            v = value(a) if unsigned else signed(value(a), w)
            return FALSE if v == highest else relation("leu" if unsigned else "le", const(w, v + 1), b)
    else:
        if is_const(b) and (value(b) if unsigned else signed(value(b), w)) == highest:
            return TRUE
        if is_const(a) and (value(a) if unsigned else signed(value(a), w)) == lowest:
            return TRUE
    return mk(kind, 1, a, b)


def negate(c: E) -> E:
    if c is TRUE:
        return FALSE
    if c is FALSE:
        return TRUE
    if c.op == "eq":
        return mk("ne", 1, *c.args)
    if c.op == "ne":
        return mk("eq", 1, *c.args)
    a, b = c.args[0], c.args[1]
    flip = {"lt": "le", "le": "lt", "ltu": "leu", "leu": "ltu"}
    if c.op in flip:
        return relation(flip[c.op], b, a)
    return mk("not", 1, c)


# ---- flags: the last flag-setting operation, as (kind, operands); cond() reads a condition code from them


@dataclass(frozen=True)
class Flags:
    # "sub" (a - b, as cmp), "incdec" (as cmp, the carry unchanged), "add" (a + b: ZF, SF and CF), "logic" (a: the
    # result, CF = OF = 0), "result" (a: only ZF and SF known)
    kind: str
    a: E
    b: E | None = None


def cond(cc: str, f: Flags | None) -> E:
    if f is None:
        raise Unsupported(f"j{cc} without flags")
    if f.kind == "incdec":
        # (inc and dec leave the carry; every other condition is cmp a, -1's or cmp a, 1's)
        if cc in ("b", "ae", "be", "a"):
            raise Unsupported(f"condition {cc} after inc or dec")
        f = Flags("sub", f.a, f.b)
    if f.kind == "sub":
        a, b = f.a, f.b
        table = {
            "e": ("eq", a, b),
            "ne": ("ne", a, b),
            "l": ("lt", a, b),
            "le": ("le", a, b),
            "g": ("gt", a, b),
            "ge": ("ge", a, b),
            "b": ("ltu", a, b),
            "be": ("leu", a, b),
            "a": ("gtu", a, b),
            "ae": ("geu", a, b),
        }
        if cc in table:
            return relation(*table[cc])
        if cc in ("s", "ns"):
            r = relation("lt", sub(a.w, a, b), const(a.w, 0))
            return r if cc == "s" else negate(r)
    zero = const(f.a.w, 0)
    if f.kind == "logic":
        r = f.a
        table = {
            "e": ("eq", r, zero),
            "ne": ("ne", r, zero),
            "s": ("lt", r, zero),
            "l": ("lt", r, zero),
            "ns": ("ge", r, zero),
            "ge": ("ge", r, zero),
            "le": ("le", r, zero),
            "g": ("gt", r, zero),
            "be": ("eq", r, zero),
            "a": ("ne", r, zero),
        }
        if cc in table:
            return relation(*table[cc])
        if cc in ("b", "ae"):
            return FALSE if cc == "b" else TRUE
    if f.kind == "add":
        r = add(f.a.w, f.a, f.b)
        if cc in ("b", "ae"):
            carry = relation("ltu", r, f.a)  # (the sum wrapped)
            return carry if cc == "b" else negate(carry)
        f = Flags("result", r)
    if f.kind == "result" and cc in ("e", "ne", "s", "ns"):
        return relation({"e": "eq", "ne": "ne", "s": "lt", "ns": "ge"}[cc], f.a, zero)
    raise Unsupported(f"condition {cc} after {f.kind} flags")


# --------------------------------------------------------------------------------------------------- value sets

# (a value known to be one of a few constants: a loop counter's range, a switch's cases; at most this many)
SET_LIMIT = 32
# (the runs of a point after which it keeps no sets)
WIDEN = 12


def _contains(e: E, k: E, memo: dict | None = None) -> bool:
    """Whether K is part of E (MEMO: what is known of E's parts, for this K only)."""
    memo = {} if memo is None else memo
    if e is k:
        return True
    if id(e) not in memo:
        memo[id(e)] = any(isinstance(a, E) and _contains(a, k, memo) for a in e.args)
    return memo[id(e)]


_keys_cache: dict = {}


def keys_in(e: E, sets: dict) -> list[E]:
    """The values with sets that are part of E (one walk, remembered for the values that have sets: what a value
    contains does not depend on the sets themselves)."""
    keys = frozenset(sets)
    cache = _keys_cache.get(keys)
    if cache is None:
        if len(_keys_cache) > 256:
            _keys_cache.clear()
        cache = _keys_cache[keys] = {}

    def walk(x: E) -> frozenset:
        found = cache.get(id(x))
        if found is None:
            found = frozenset((x,)) if x in keys else frozenset()
            for a in x.args:
                if isinstance(a, E):
                    found |= walk(a)
            cache[id(x)] = found
        return found

    return list(walk(e))


def substitute(e: E, k: E, c: int) -> E | None:
    """E with K replaced by the constant C, rebuilt in canonical form; None where an operation cannot be rebuilt."""
    memo: dict = {}
    inside: dict = {}

    def go(x: E) -> E | None:
        if x is k:
            return const(k.w, c)
        if not _contains(x, k, inside):
            return x
        if id(x) in memo:
            return memo[id(x)]
        args = [go(a) if isinstance(a, E) else a for a in x.args]
        if any(a is None for a in args):
            out = None
        else:
            out = _rebuild(x, args)
        memo[id(x)] = out
        return out

    return go(e)


def _rebuild(x: E, args: list) -> E | None:
    op, w = x.op, x.w
    if op == "lin":
        return add(w, const(w, args[0]), *(scale_by(w, args[i + 1], args[i]) for i in range(1, len(args), 2)))
    if op == "mul":
        return mul(w, *args)
    if op in ("and", "or", "xor"):
        return bitop(op, w, *args)
    if op == "not":
        return negate(args[0]) if w == 1 else bitnot(w, args[0])
    if op == "ext":
        return extract(args[0], args[1], w)
    if op == "ins":
        return insert(args[0], args[1], args[2])
    if op in ("zext", "sext"):
        return (zext if op == "zext" else sext)(w, args[0])
    if op == "cat":
        return cat(args)
    if op in ("shr", "sar"):
        return shift(op, w, args[0], const(8, args[1]))
    if op in ("shlv", "shrv", "sarv"):
        return shift(op[:-1], w, args[0], args[1])
    if op in ("eq", "ne"):
        r = equal(args[0], const(args[0].w, 0))
        return r if op == "eq" else negate(r)
    if op in ("lt", "le", "ltu", "leu"):
        return relation(op, args[0], args[1])
    if op == "load":
        return load(args[0], args[1], w // 8)
    if op == "store":
        return store(args[0], args[1], args[2])
    if op in ("call", "callmem", "ret", "ret2", "argafter", "block", "blockmem", "string", "out"):
        return mk(op, w, *args)
    if op in ("sdiv", "srem", "udiv", "urem") and all(is_const(a) for a in args) and value(args[1]):
        a, b = value(args[0]), value(args[1])
        if op[0] == "s":
            a, b = signed(a, w), signed(b, w)
            q = abs(a) // abs(b) * (1 if (a < 0) == (b < 0) else -1)
            return const(w, q if op == "sdiv" else a - q * b)
        return const(w, a // b if op == "udiv" else a % b)
    return None


def values_of(e: E, sets: dict) -> frozenset | None:
    """The values E can have, when it is a constant or computed from one value of a known set."""
    if is_const(e):
        return frozenset((value(e),))
    if e in sets:
        return sets[e]
    keys = keys_in(e, sets)
    if len(keys) != 1:
        return None
    out = set()
    for c in sets[keys[0]]:
        r = substitute(e, keys[0], c)
        if r is None or not is_const(r):
            return None
        out.add(value(r))
    return frozenset(out)


def refine(sets: dict, condition: E) -> dict | None:
    """SETS where CONDITION holds (None: it cannot hold): the sets it reads filtered, and a value it says is a
    constant known as one."""
    out = dict(sets)
    for k in keys_in(condition, sets):
        kept = set()
        for c in sets[k]:
            r = substitute(condition, k, c)
            if r is not FALSE:
                kept.add(c)
        if not kept:
            return None
        out[k] = frozenset(kept)
    if condition.op == "eq":
        t, c = linear(condition.args[0])
        if len(t) == 1:
            ((term, k),) = t.items()
            w = condition.args[0].w
            if k in (1, mask(w)):
                v = (-c if k == 1 else c) & mask(w)
                if term in out and v not in out[term]:
                    return None
                out[term] = frozenset((v,))
    return out


def _range(condition: E) -> tuple[E, frozenset] | None:
    """(x, the values x can take) where CONDITION bounds a value x + k by a constant to at most SET_LIMIT values; x a
    value extended from a narrower one is taken as that one."""
    if condition.op not in ("le", "leu") or is_const(condition.args[0]) == is_const(condition.args[1]):
        return None
    a, b = condition.args
    w = a.w
    x, c = (a, b) if is_const(b) else (b, a)
    lowest, highest = (0, mask(w)) if condition.op == "leu" else (-(1 << (w - 1)), (1 << (w - 1)) - 1)
    bound = value(c) if condition.op == "leu" else signed(value(c), w)
    lo, hi = (lowest, bound) if x is a else (bound, highest)
    t, k = linear(x)
    if len(t) != 1 or hi - lo >= SET_LIMIT:
        return None
    ((term, scale),) = t.items()
    if scale != 1:
        return None
    found = {(v - k) & mask(w) for v in range(lo, hi + 1)}
    if term.op in ("sext", "zext"):
        inner = term.args[0]
        n = inner.w
        if term.op == "sext":
            found = {v & mask(n) for v in found if signed(v, w) == signed(v & mask(n), n)}
        else:
            found = {v for v in found if v < 1 << n}
        term = inner
    return term, frozenset(found)


def path_sets(sets: dict, conditions) -> dict | None:
    """SETS where every one of CONDITIONS holds (None: they cannot all hold): each filters the sets it reads, and a
    bound to a few values makes a set, which the others then filter."""
    out = sets
    for _ in range(2):
        for c in conditions:
            out = refine(out, c)
            if out is None:
                return None
            bounded = _range(c)
            if bounded is not None:
                term, found = bounded
                found = found & out[term] if term in out else found
                if not found:
                    return None
                out = {**out, term: found}
    return out


def decide(condition: E, sets: dict) -> E:
    """TRUE or FALSE when CONDITION is decided by the sets of the values it reads, else CONDITION."""
    if not sets or is_const(condition):
        return condition
    found = values_of(condition, sets)
    if found is not None and len(found) == 1:
        return TRUE if 1 in found else FALSE
    return condition


def alike(a: E | None, b: E | None, sets: dict) -> bool:
    """Whether two values are the same: one expression, or one for every value of the one set they read."""
    if a is b:
        return True
    if a is None or b is None or not sets:
        return False
    keys = list(set(keys_in(a, sets)) | set(keys_in(b, sets)))
    if len(keys) != 1:
        return False
    for c in sets[keys[0]]:
        x, y = substitute(a, keys[0], c), substitute(b, keys[0], c)
        if x is None or x is not y:
            return False
    return True


def signature(v: E, sets: dict) -> tuple | None:
    """V for each value of the one set it reads (None: it reads none, or more than one, or cannot be rebuilt)."""
    keys = keys_in(v, sets)
    if len(keys) != 1:
        return None
    out = tuple(substitute(v, keys[0], c) for c in sorted(sets[keys[0]]))
    return None if any(x is None for x in out) else (keys[0], out)


def same_condition(a: E, b: E, sets: dict) -> str | None:
    """ "same" or "negated" when two conditions agree (or disagree) for every value of the set they read."""
    if alike(a, b, sets):
        return "same"
    if alike(a, negate(b), sets):
        return "negated"
    return None


# ------------------------------------------------------------------------------------------------------- memory


def _offset(a: E, b: E) -> int | None:
    """b - a when it is a constant."""
    d = sub(32, b, a)
    return signed(value(d), 32) if is_const(d) else None


def _base(addr: E) -> E | None:
    """The frame object an address is inside, or None (a global's, or one computed from something else)."""
    t, _ = linear(addr)
    frames = [term for term in t if term.op == "frame" and t[term] == 1]
    # (an address computed from a frame object's stays inside it: C's rule for pointer arithmetic)
    return frames[0] if len(frames) == 1 else None


# (addresses taken as never overlapping, a proof's named assumption: {frozenset of the two addresses: its index}; and
# the indexes the proof used)
_assumed_disjoint: dict[frozenset, int] = {}
_disjoint_used: set[int] = set()


def _disjoint(a: E, n: int, b: E, m: int) -> bool:
    d = _offset(a, b)
    if d is not None:
        if (d < n and d + m > 0) and frozenset((a, b)) in _assumed_disjoint:
            raise Differs(f"accesses assumed apart overlap: {a.render(200)} and {b.render(200)}")
        return d >= n or d + m <= 0
    if (k := _assumed_disjoint.get(frozenset((a, b)))) is not None:
        _disjoint_used.add(k)
        return True
    fa, fb = _base(a), _base(b)
    if fa is not None and fb is not None:
        return fa is not fb
    # (a frame object is not a global)
    return fa is not None and is_const(b) or fb is not None and is_const(a)


def store(mem: E, addr: E, v: E) -> E:
    n = v.w // 8
    if mem.op == "store":
        inner, a, old = mem.args
        if a is addr and old.w == v.w:
            return store(inner, addr, v)  # (an overwritten store)
        if _disjoint(a, old.w // 8, addr, n) and addr.key < a.key:
            return store(store(inner, addr, v), a, old)
    return mk("store", 0, mem, addr, v)


def load(mem: E, addr: E, n: int) -> E:
    while mem.op == "store":
        inner, a, v = mem.args
        d = _offset(a, addr)
        if d is not None and 0 <= d and d + n <= v.w // 8:
            return extract(v, 8 * d, 8 * n)
        if not _disjoint(a, v.w // 8, addr, n):
            break
        mem = inner
    return mk("load", 8 * n, mem, addr)


# ------------------------------------------------------------------------------------------------- the two codes


@dataclass
class Code:
    """One function's code as the model reads it."""

    name: str
    address: int
    code: bytes
    image: PEFile  # the executable its data addresses are in
    jump_tables: dict[int, list[int]] = field(default_factory=dict)  # table address -> targets (own code)
    constants: list[tuple[int, int]] = field(default_factory=list)  # [start, end) of compiler-generated constants
    # (the size of the largest local whose address the source takes, an aggregate's included; None: not known, and an
    # object then reaches to the next one)
    object_limit: int | None = None
    # (the most argument words a call through a function pointer passes in the function's source, None: not known)
    indirect_arguments: int | None = None

    def __post_init__(self):
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        self.instructions = {i.address: i for i in md.disasm(self.code, self.address)}
        if sum(i.size for i in self.instructions.values()) != len(self.code):
            raise Unsupported(f"{self.name}: not all of its bytes disassemble")
        # (the jump tables its code indexes: a table read from the executable ends where the next one starts)
        self.tables = {
            i.operands[0].mem.disp & 0xFFFFFFFF
            for i in self.instructions.values()
            if i.mnemonic == "jmp" and i.operands[0].type == x86.X86_OP_MEM
        }
        self.esp: dict[int, int] = {}
        self.frame_bottom = 0
        self.pushed = 0
        self._scan()

    def is_constant(self, address: int, n: int) -> bool:
        return any(lo <= address and address + n <= hi for lo, hi in self.constants)

    def read_constant(self, address: int, n: int) -> int:
        return int.from_bytes(self.image.read(address, n), "little")

    # ---- the stack pointer at each instruction, and the address-taken stack objects

    def _successors(self, i) -> list[int]:
        m = i.mnemonic
        nxt = i.address + i.size
        if m == "ret":
            return []
        if m == "jmp":
            op = i.operands[0]
            if op.type == x86.X86_OP_IMM:
                return [op.imm]
            table = op.mem.disp & 0xFFFFFFFF if op.type == x86.X86_OP_MEM else None
            if table in self.jump_tables:
                return list(self.jump_tables[table])
            return self.table_targets(table) if table is not None else []
        if m.startswith("j"):
            return [i.operands[0].imm, nxt]
        return [nxt]

    def table_targets(self, table: int) -> list[int]:
        """The targets of a jump table in the executable: its entries while they point into this function, up to the
        next table the function indexes (MWCC lays a function's tables out one after another)."""
        out = []
        while not out or table + 4 * len(out) not in self.tables:
            try:
                t = self.image.word(table + 4 * len(out))
            except (ValueError, struct.error):
                break
            if not self.address <= t < self.address + len(self.code):
                break
            out.append(t)
        return out

    def _scan(self):
        esp = {self.address: 0}
        pending = [self.address]
        while pending:
            a = pending.pop()
            i = self.instructions.get(a)
            if i is None:
                raise Unsupported(f"{self.name}: a branch to {a:#x}, not an instruction of the function")
            d = esp[a] + stack_effect(i, self.image)
            for s in self._successors(i):
                if s in esp:
                    if esp[s] != d:
                        raise Unsupported(f"{self.name}: the stack pointer differs at {s:#x} on two paths")
                    continue
                esp[s] = d
                pending.append(s)
        self.esp = esp
        # (the frame: the registers saved first, then the locals; below it, arguments being pushed)
        a = self.address
        while a in self.instructions:
            i = self.instructions[a]
            if i.mnemonic == "push" and i.operands[0].type == x86.X86_OP_REG:
                self.pushed += 4
            elif i.mnemonic == "sub" and i.reg_name(i.operands[0].reg) == "esp":
                a += i.size
                break
            else:
                break
            a += i.size
        self.frame_bottom = esp.get(a, 0)
        self.bases: set[int] = set()
        self.arguments = 4  # (the end of the arguments it reaches)
        for a, i in self.instructions.items():
            if a not in esp:
                continue
            for op in i.operands:
                if op.type != x86.X86_OP_MEM or op.mem.base != x86.X86_REG_ESP:
                    continue
                k = esp[a] + op.mem.disp
                if i.mnemonic == "lea" or op.mem.index:
                    self.bases.add(k)
                if k >= 4:
                    self.arguments = max(self.arguments, k + max(op.size, 1) + 3 & ~3)
        # (each argument and frame word is a location of every state: thousands would take a proof past its memory
        # between two checks of it)
        if self.arguments > ARGUMENTS_LIMIT:
            raise Unsupported(f"{self.name}: an access {self.arguments:#x} bytes above its return address")
        if -self.frame_bottom > FRAME_LIMIT:
            raise Unsupported(f"{self.name}: a frame of {-self.frame_bottom:#x} bytes")
        self.bases = {b for b in self.bases if b < -self.pushed or b >= 4}

    def region(self, k: int) -> int | None:
        """The address-taken object entry offset K is in (its base), if any."""
        below = [b for b in self.bases if b <= k]
        if not below:
            return None
        b = max(below)
        if b < 0 and k >= -self.pushed:
            return None
        if self.object_limit is not None and k >= b + self.object_limit:
            return None  # (past the largest object the function's source takes the address of)
        return b


# (the most bytes of arguments a function's code may reach above its return address, and of its frame)
ARGUMENTS_LIMIT = 0x1000
FRAME_LIMIT = 0x4000
_pops: dict[tuple[str, int], int] = {}


def callee_pops(image: PEFile, i) -> int:
    """The bytes of arguments a call's callee pops itself (a __stdcall function's `ret N`): read from the first
    return of the code at a direct call's target. An indirect call's callee is taken to be cdecl (a function pointer
    of MWCC's code), except through the import table (Win32's functions are __stdcall), which is not covered; the
    words pushed for it are compared as the call's arguments, and the stack's balance at the return is checked."""
    op = i.operands[0]
    if op.type != x86.X86_OP_IMM:
        slots = {x.slot for x in getattr(image, "imports", ())}
        if op.type == x86.X86_OP_MEM and not op.mem.base and not op.mem.index and op.mem.disp & 0xFFFFFFFF in slots:
            raise Unsupported("a call through the import table (a __stdcall callee whose pops are not known)")
        return 0
    key = (str(image.path), op.imm)
    if key not in _pops:
        md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
        md.detail = True
        try:
            code = image.read(op.imm, 0x10000)
        except ValueError:
            try:
                code = image.read(op.imm, image.section_for_address(op.imm).end - op.imm)
            except ValueError:
                raise Unsupported(f"a call to {op.imm:#x}, outside the executable") from None
        pops = None
        for x in md.disasm(code, op.imm):
            if x.address == op.imm and x.mnemonic == "jmp" and x.operands[0].type != x86.X86_OP_IMM:
                # (an import's thunk: its callee's code, and so its return, is not in the executable)
                raise Unsupported(f"a call to the thunk at {op.imm:#x} (a __stdcall callee whose pops are not known)")
            if x.mnemonic == "ret":
                pops = x.operands[0].imm if x.operands else 0
                break
        if pops is None:
            raise Unsupported(f"no return in the callee at {op.imm:#x}")
        _pops[key] = pops
    return _pops[key]


def stack_effect(i, image: PEFile | None = None) -> int:
    m = i.mnemonic
    if m == "call":
        return callee_pops(image, i) if image is not None else 0
    if m == "push":
        return -4
    if m == "pop":
        return 4
    if m in ("sub", "add") and i.operands[0].type == x86.X86_OP_REG and i.reg_name(i.operands[0].reg) == "esp":
        if i.operands[1].type != x86.X86_OP_IMM:
            raise Unsupported(f"{m} esp by a register at {i.address:#x}")
        k = i.operands[1].imm
        return -k if m == "sub" else k
    if m == "ret":
        return 4
    return 0


# ---------------------------------------------------------------------------------------------------- the state

REGISTERS = ("eax", "ecx", "edx", "ebx", "esi", "edi", "ebp")
CALLEE_SAVED = ("ebx", "esi", "edi", "ebp")
_PARTS = {}
for _full, _x, _l, _h in (
    ("eax", "ax", "al", "ah"),
    ("ecx", "cx", "cl", "ch"),
    ("edx", "dx", "dl", "dh"),
    ("ebx", "bx", "bl", "bh"),
):
    _PARTS.update({_full: (_full, 0, 32), _x: (_full, 0, 16), _l: (_full, 0, 8), _h: (_full, 8, 8)})
for _full, _x in (("esi", "si"), ("edi", "di"), ("ebp", "bp")):
    _PARTS.update({_full: (_full, 0, 32), _x: (_full, 0, 16)})


@dataclass
class State:
    regs: dict[str, E]
    slots: dict[int, E]  # entry offset -> the value stored there (its width says how many bytes)
    esp: int
    mem: E
    flags: Flags | None = None
    sets: dict = field(default_factory=dict)  # value -> the constants it can be (value sets)

    def copy(self) -> State:
        return State(dict(self.regs), dict(self.slots), self.esp, self.mem, self.flags, dict(self.sets))


@dataclass
class End:
    """How a segment ends."""

    kind: str  # "branch", "switch", "ret", "noreturn" (a call that does not return)
    at: int  # the instruction
    state: State
    condition: E | None = None
    targets: tuple[int, ...] = ()
    index: E | None = None
    pop: int = 0


class Machine:
    """Executes one side's code on a State."""

    def __init__(
        self, code: Code, frames: dict[int, E], noreturn=frozenset(), parameters=None, returns=None, disjoint=()
    ):
        self.code, self.frames = code, frames  # frames: this side's object base -> its address expression
        # (the named assumptions that two instructions' memory accesses never overlap: each instruction's address ->
        # (the assumption's index, whether it is the first of its pair); and the addresses each first one accessed)
        self.disjoint = {a: (k, j == 0) for k, pair in enumerate(disjoint) for j, a in enumerate(pair)}
        self.firsts: dict[int, set[E]] = {}
        self.noreturn = noreturn  # the functions that never return (a call to one ends the path)
        # (callee -> the bits of each argument word it reads, None for all)
        self.parameters: dict[int, tuple[int | None, ...]] = parameters or {}
        # (callee -> the bits of its return value, 0 for void; a callee not listed returns 32, as does an indirect call)
        self.returns: dict[int, int] = returns or {}

    # ---- operands

    def reg(self, s: State, name: str) -> E:
        if name == "esp":
            # (the stack pointer as a value: an address in the frame, followed while it stays in registers)
            return add(32, SP, const(32, s.esp))
        if name not in _PARTS:
            raise Unsupported(f"register {name}")
        full, lo, n = _PARTS[name]
        return extract(s.regs[full], lo, n)

    def set_reg(self, s: State, name: str, v: E):
        if name == "esp":
            raise Unsupported("esp written")
        if name not in _PARTS:
            raise Unsupported(f"register {name}")
        full, lo, n = _PARTS[name]
        s.regs[full] = v if n == 32 else insert(s.regs[full], lo, extract(v, 0, n))

    def address(self, s: State, i, op) -> tuple[str, object]:
        """("slot", entry offset) or ("mem", address expression)."""
        m = op.mem
        if m.segment:
            raise Unsupported("a segment register")
        if m.index == x86.X86_REG_ESP:
            raise Unsupported("esp as an index")
        terms = [const(32, m.disp)]
        if m.base:
            terms.append(self.reg(s, i.reg_name(m.base)))
        if m.index:
            terms.append(scale_by(32, self.reg(s, i.reg_name(m.index)), m.scale))
        return self.place(add(32, *terms))

    def place(self, addr: E) -> tuple[str, object]:
        """Where an address is: a stack slot (by its entry offset), or memory (a frame object's address made the
        object's)."""
        t, k = linear(addr)
        if not t.get(SP):
            return "mem", addr
        if t[SP] != 1:
            raise Unsupported("a scaled stack address")
        k = signed(k, 32)
        b = self.code.region(k)
        rest = {term: c for term, c in t.items() if term is not SP}
        if b is None:
            if rest:
                raise Unsupported("an indexed stack access outside any object")
            return "slot", k
        return "mem", add(32, self.frames[b], const(32, k - b), make_linear(32, rest, 0))

    def lea(self, s: State, i, op) -> E:
        where, a = self.address(s, i, op)
        if where == "slot":
            return add(32, SP, const(32, a))
        return a

    def read(self, s: State, i, op, n: int | None = None) -> E:
        n = n or op.size
        if op.type == x86.X86_OP_REG:
            return self.reg(s, i.reg_name(op.reg))
        if op.type == x86.X86_OP_IMM:
            return const(8 * n, op.imm)
        where, a = self.address(s, i, op)
        if where == "slot":
            return self.slot_load(s, a, n)
        self.accessed(i, a)
        return self.mem_load(s, a, n)

    def write(self, s: State, i, op, v: E):
        if op.type != x86.X86_OP_REG and has_sp(v):
            raise Unsupported(f"a stack address stored at {i.address:#x}")
        if op.type == x86.X86_OP_REG:
            self.set_reg(s, i.reg_name(op.reg), v)
            return
        where, a = self.address(s, i, op)
        if where == "slot":
            self.slot_store(s, a, v)
        else:
            self.accessed(i, a)
            s.mem = store(s.mem, a, v)

    def accessed(self, i, a: E):
        """Note a memory access at I, for an assumption that names I: the second instruction of a pair is taken never
        to overlap what the first accessed."""
        if (named := self.disjoint.get(i.address)) is None:
            return
        k, first = named
        if first:
            self.firsts.setdefault(k, set()).add(a)
        else:
            for b in self.firsts.get(k, ()):
                if a is not b:
                    _assumed_disjoint[frozenset((a, b))] = k

    def mem_load(self, s: State, addr: E, n: int) -> E:
        if is_const(addr) and self.code.is_constant(value(addr), n):
            return const(8 * n, self.code.read_constant(value(addr), n))
        return load(s.mem, addr, n)

    # ---- stack slots

    def slot_store(self, s: State, k: int, v: E):
        n = v.w // 8
        for start in [x for x in s.slots if x < k + n and x + s.slots[x].w // 8 > k]:
            old = s.slots.pop(start)
            m = old.w // 8
            if start < k:
                s.slots[start] = extract(old, 0, 8 * (k - start))
            if start + m > k + n:
                s.slots[k + n] = extract(old, 8 * (k + n - start), 8 * (start + m - k - n))
        s.slots[k] = v

    def slot_load(self, s: State, k: int, n: int) -> E:
        v = s.slots.get(k)
        if v is not None and v.w == 8 * n:
            return v
        parts, at = [], k
        while at < k + n:
            start = next((x for x in s.slots if x <= at < x + s.slots[x].w // 8), None)
            if start is None:
                # (a byte never stored: unknown, but the same unknown each time it is read)
                s.slots[at] = undef(8, f"stack {at:#x}")
                start = at
            v = s.slots[start]
            take = min(start + v.w // 8, k + n) - at
            parts.append(extract(v, 8 * (at - start), 8 * take))
            at += take
        return cat(parts)

    def drop_below(self, s: State):
        for k in [k for k in s.slots if k < s.esp]:
            del s.slots[k]

    # ---- execution

    def run(self, s: State, start: int) -> End:
        """Execute from START until a conditional branch, a jump table or a return."""
        a, steps = start, 0
        while True:
            steps += 1
            if steps % 256 == 0:
                check_budget()
            if steps > 20000:
                raise Unsupported(f"no end to the code from {start:#x}")
            i = self.code.instructions.get(a)
            if i is None:
                raise Unsupported(f"{a:#x} is not an instruction of {self.code.name}")
            if self.code.esp.get(a, s.esp) != s.esp:
                raise Unsupported(f"the stack pointer at {a:#x}")
            try:
                end = self.step(s, i)
            except Unsupported as e:
                raise Unsupported(f"{e} ({i.address:#x}: {i.mnemonic} {i.op_str})") from None
            if isinstance(end, End):
                return end
            a = end if end is not None else a + i.size

    def step(self, s: State, i):  # (one case per instruction)
        m, ops = i.mnemonic, i.operands
        nxt = i.address + i.size
        if m.startswith("j") and m != "jmp":
            c = decide(cond(m[1:], s.flags), s.sets)
            if c is TRUE:
                return ops[0].imm
            if c is FALSE:
                return nxt
            return End("branch", i.address, s, condition=c, targets=(ops[0].imm, nxt))
        if m == "jmp":
            op = ops[0]
            if op.type == x86.X86_OP_IMM:
                return op.imm
            if op.type == x86.X86_OP_MEM and op.mem.index and not op.mem.base and op.mem.scale == 4:
                table = op.mem.disp & 0xFFFFFFFF
                targets = self.code.jump_tables.get(table) or self.code.table_targets(table)
                if not targets:
                    raise Unsupported(f"a jump table at {table:#x} the model cannot read")
                index = self.reg(s, i.reg_name(op.mem.index))
                cases = values_of(index, s.sets)
                if cases is not None and len(cases) == 1 and value_in(cases, len(targets)):
                    return targets[next(iter(cases))]
                return End("switch", i.address, s, index=index, targets=tuple(targets))
            raise Unsupported("an indirect jump")
        if m == "ret":
            return End("ret", i.address, s, pop=ops[0].imm if ops else 0)
        if m in ("nop",):
            return None
        if m in ("push", "pop") and ops[0].type != x86.X86_OP_IMM and ops[0].size != 4:
            raise Unsupported("a push or pop of other than four bytes")
        if m == "push":
            v = self.read(s, i, ops[0], 4) if ops[0].type != x86.X86_OP_IMM else const(32, ops[0].imm)
            s.esp -= 4
            self.slot_store(s, s.esp, v)
            return None
        if m == "pop":
            v = self.slot_load(s, s.esp, 4)
            s.esp += 4
            self.drop_below(s)
            self.write(s, i, ops[0], v)
            return None
        if m == "call":
            return self.call(s, i)
        if m in ("add", "sub") and ops[0].type == x86.X86_OP_REG and i.reg_name(ops[0].reg) == "esp":
            s.esp += stack_effect(i)
            self.drop_below(s)
            s.flags = None
            return None
        if m == "mov":
            self.write(s, i, ops[0], self.read(s, i, ops[1], ops[0].size))
            return None
        if m in ("movsx", "movzx"):
            v = self.read(s, i, ops[1])
            self.write(s, i, ops[0], (sext if m == "movsx" else zext)(8 * ops[0].size, v))
            return None
        if m == "lea":
            self.write(s, i, ops[0], self.lea(s, i, ops[1]))
            return None
        if m in ("add", "sub", "and", "or", "xor", "cmp", "test", "adc"):
            w = 8 * ops[0].size
            a = self.read(s, i, ops[0])
            b = self.read(s, i, ops[1], ops[0].size)
            if m == "adc":
                if s.flags is None:
                    raise Unsupported("adc without flags")
                carry = zext(w, cond("b", s.flags))
                r = add(w, a, b, carry)
                s.flags = Flags("result", r)
                self.write(s, i, ops[0], r)
                return None
            if m in ("sub", "cmp"):
                r = sub(w, a, b)
                s.flags = Flags("sub", a, b)
            elif m == "add":
                r = add(w, a, b)
                s.flags = Flags("add", a, b)
            else:
                r = bitop({"test": "and"}.get(m, m), w, a, b)
                s.flags = Flags("logic", r)
            if m not in ("cmp", "test"):
                self.write(s, i, ops[0], r)
            return None
        if m in ("inc", "dec"):
            w = 8 * ops[0].size
            a = self.read(s, i, ops[0])
            one = const(w, 1)
            r = add(w, a, one) if m == "inc" else sub(w, a, one)
            # (for every condition but the carry's, inc and dec set the flags cmp a, -1 and cmp a, 1 do)
            s.flags = Flags("incdec", a, const(w, -1) if m == "inc" else one)
            self.write(s, i, ops[0], r)
            return None
        if m in ("neg", "not"):
            w = 8 * ops[0].size
            a = self.read(s, i, ops[0])
            r = scale_by(w, a, -1) if m == "neg" else bitnot(w, a)
            if m == "neg":
                s.flags = Flags("result", r)
            self.write(s, i, ops[0], r)
            return None
        if m in ("shl", "sar", "shr", "rol", "ror"):
            w = 8 * ops[0].size
            a = self.read(s, i, ops[0])
            n = self.read(s, i, ops[1], 1) if len(ops) > 1 else const(8, 1)
            if m in ("rol", "ror"):
                r = mk(m, w, a, n)
                s.flags = None
            else:
                r = shift(m, w, a, n)
                s.flags = Flags("result", r) if is_const(n) and value(n) & 31 else None
            self.write(s, i, ops[0], r)
            return None
        if m == "imul":
            if len(ops) == 1:
                raise Unsupported("one-operand imul")
            w = 8 * ops[0].size
            a = self.read(s, i, ops[1] if len(ops) == 3 else ops[0])
            b = self.read(s, i, ops[2] if len(ops) == 3 else ops[1], ops[0].size)
            self.write(s, i, ops[0], mul(w, a, b))
            s.flags = None
            return None
        if m == "cdq":
            s.regs["edx"] = shift("sar", 32, s.regs["eax"], const(8, 31))
            return None
        if m in ("idiv", "div"):
            if ops[0].size != 4:
                raise Unsupported("a narrow division")
            d = self.read(s, i, ops[0])
            eax, edx = s.regs["eax"], s.regs["edx"]
            wide = edx is shift("sar", 32, eax, const(8, 31)) if m == "idiv" else edx is const(32, 0)
            if not wide:
                raise Unsupported("a 64-bit dividend")
            s.regs["eax"] = mk("sdiv" if m == "idiv" else "udiv", 32, eax, d)
            s.regs["edx"] = mk("srem" if m == "idiv" else "urem", 32, eax, d)
            s.flags = None
            return None
        if m.startswith("set"):
            self.write(s, i, ops[0], zext(8, cond(m[3:], s.flags)))
            return None
        if m in ("movsb", "movsw", "movsd", "stosb", "stosw", "stosd"):
            self.string(s, m[:4], {"b": 1, "w": 2, "d": 4}[m[-1]])
            return None
        if m.startswith("rep "):
            op = m[4:]
            if op[:4] in ("movs", "stos"):
                n = {"b": 1, "w": 2, "d": 4}[op[-1]]
                count = s.regs["ecx"]
                if is_const(count) and value(count) <= 64:
                    for _ in range(value(count)):
                        self.string(s, op[:4], n)
                else:
                    # (a block the model does not follow byte by byte: one effect of its inputs, which must be in
                    # memory: stack slots are values the effect would not see)
                    if has_sp(s.regs["edi"]) or op[:4] == "movs" and has_sp(s.regs["esi"]):
                        raise Unsupported("a block copy or fill of stack slots")
                    source = s.regs["esi"] if op[:4] == "movs" else s.regs["eax"]
                    node = mk("block", 0, op, s.mem, s.regs["edi"], source, count)
                    s.mem = mk("blockmem", 0, node)
                    s.regs["edi"] = add(32, s.regs["edi"], scale_by(32, count, n))
                    if op[:4] == "movs":
                        s.regs["esi"] = add(32, s.regs["esi"], scale_by(32, count, n))
                s.regs["ecx"] = const(32, 0)
                return None
            raise Unsupported(f"instruction {m}")
        if m in ("repe cmpsb", "repne scasb"):
            if has_sp(s.regs["edi"]) or has_sp(s.regs["esi"]):
                raise Unsupported("a string compare of stack slots")
            inputs = (s.mem, s.regs["esi"], s.regs["edi"], s.regs["ecx"], extract(s.regs["eax"], 0, 8))
            node = mk("string", 0, m, *inputs)
            for r in ("esi", "edi", "ecx"):
                s.regs[r] = mk("out", 32, node, r)
            s.flags = Flags("sub", mk("out", 8, node, "a"), mk("out", 8, node, "b"))
            return None
        raise Unsupported(f"instruction {m}")

    def string(self, s: State, op: str, n: int):
        edi = s.regs["edi"]
        if op == "movs":
            where, a = self.place(s.regs["esi"])
            v = self.slot_load(s, a, n) if where == "slot" else self.mem_load(s, a, n)
            s.regs["esi"] = add(32, s.regs["esi"], const(32, n))
        else:
            v = extract(s.regs["eax"], 0, 8 * n)
        if has_sp(v):
            raise Unsupported("a stack address stored")
        where, a = self.place(edi)
        if where == "slot":
            self.slot_store(s, a, v)
        else:
            s.mem = store(s.mem, a, v)
        s.regs["edi"] = add(32, edi, const(32, n))

    def call(self, s: State, i):
        op = i.operands[0]
        if op.type == x86.X86_OP_IMM:
            target = const(32, op.imm)
        else:
            target = self.read(s, i, op, 4)
        if s.esp > self.code.frame_bottom:
            raise Unsupported("a call above the frame")
        args = [self.slot_load(s, k, 4) for k in range(s.esp, self.code.frame_bottom, 4)]
        # (a char or short argument: the callee reads only its low bits, so the rest of its word is not compared)
        widths = self.parameters.get(value(target), ()) if is_const(target) else ()
        if len(args) < len(widths):
            # (arguments stored in the frame rather than pushed: the model would not compare them)
            raise Unsupported("fewer argument words below the frame than the callee's parameters")
        if not is_const(target) and (self.code.indirect_arguments is None or len(args) < self.code.indirect_arguments):
            # (the same for a call through a pointer, against the most words the source's such calls pass)
            raise Unsupported("an indirect call with fewer argument words below the frame than the source passes")
        args = [extract(a, 0, widths[n]) if n < len(widths) and widths[n] else a for n, a in enumerate(args)]
        if any(has_sp(a) for a in args) or has_sp(target):
            raise Unsupported("a stack address passed to a call")
        node = mk("call", 0, target, s.mem, *args)
        s.mem = mk("callmem", 0, node)
        if is_const(target) and value(target) in self.noreturn:
            return End("noreturn", i.address, s)
        # (what a callee returns: only as many bits as its type has; the rest of eax and edx is what it left)
        bits = self.returns.get(value(target), 32) if is_const(target) else 32
        s.regs["eax"] = undef(32, "eax after a call")
        s.regs["edx"] = undef(32, "edx after a call")
        if bits:
            s.regs["eax"] = insert(s.regs["eax"], 0, mk("ret", min(bits, 32), node))
        if bits > 32:
            s.regs["edx"] = mk("ret2", 32, node)
        s.regs["ecx"] = undef(32, "ecx after a call")
        # (the callee may change its arguments: what it leaves there is the call's)
        for k in [k for k in s.slots if s.esp <= k < self.code.frame_bottom]:
            del s.slots[k]
        for k in range(s.esp, self.code.frame_bottom, 4):
            s.slots[k] = mk("argafter", 32, node, k - s.esp)
        s.esp += callee_pops(self.code.image, i)
        self.drop_below(s)
        s.flags = None
        return None


SP = mk("sp", 32)


def has_sp(e: E, seen: dict | None = None) -> bool:
    """Whether E is computed from the stack pointer's value."""
    seen = {} if seen is None else seen
    if e is SP:
        return True
    if id(e) not in seen:
        seen[id(e)] = False
        seen[id(e)] = any(isinstance(a, E) and has_sp(a, seen) for a in e.args)
    return seen[id(e)]


# ------------------------------------------------------------------------------------------------------- the proof


@dataclass
class Point:
    p: int
    q: int
    index: int
    esp: tuple[int, int]
    ways: dict = field(default_factory=dict)  # (from point, edge) -> what the states agree on coming that way
    done: object = None  # the agreement it was last run with
    runs: int = 0


def _locations(s: State):
    """(location, value) of what a state holds: each register whole and its low half and byte (a byte loaded into
    cl is a value even when the rest of ecx is not), and each stack slot."""
    for r in REGISTERS:
        v = s.regs[r]
        yield ("r", r, 32), v
        yield ("r", r, 16), extract(v, 0, 16)
        if r in ("eax", "ecx", "edx", "ebx"):
            yield ("r", r, 8), extract(v, 0, 8)
    for k, v in s.slots.items():
        yield ("s", k, v.w), v
        # (and its parts, so a slot stored whole on one way in and in parts on another keeps what they share)
        n = v.w // 8
        for j in range(n):
            if n > 1:
                yield ("s", k + j, 8), extract(v, 8 * j, 8)
        for j in range(0, n - 1, 2):
            if n > 2:
                yield ("s", k + j, 16), extract(v, 8 * j, 16)


class Agreement:
    """What two states (the ends of a pair of segments) agree on, for each way out: pairs of locations holding one
    value or one plus a constant, each side's locations holding a value known everywhere, those holding an extended
    narrower value, and those holding one of a set of constants. The states' locations are listed once, and what does
    not depend on a way's value sets is worked out once. With ASSUMED (prove(uninitialized=True)'s leaves), each pair
    also carries the assumptions its values rest on, so a value that crosses a join as a new leaf keeps them."""

    def __init__(self, sp: State, sq: State, assumed: dict | None = None):
        self.assumed = assumed
        self.lp, self.lq = list(_locations(sp)), list(_locations(sq))
        by_value = collections.defaultdict(list)
        for loc, v in self.lq:
            by_value[v].append(loc)
        self.pairs = frozenset((lp, lq, 0) for lp, v in self.lp for lq in by_value.get(v, ()))
        # (and word locations whose values differ by a constant: one function keeping i where the other keeps i + 1)
        by_base = collections.defaultdict(list)
        for loc, v in self.lq:
            if v.w == 32 and not is_const(v):
                t, c = linear(v)
                by_base[make_linear(32, t, 0)].append((loc, c))
        offsets = set()
        for lp, v in self.lp:
            if v.w == 32 and not is_const(v):
                t, c = linear(v)
                found = by_base.get(make_linear(32, t, 0), ())
                offsets.update((lp, lq, (c - d) & 0xFFFFFFFF) for lq, d in found if c != d)
        self.pairs |= offsets
        self.known_p = {loc: v for loc, v in self.lp if not v.local}
        self.known_q = {loc: v for loc, v in self.lq if not v.local}
        self.shape_p = {loc: shape(v) for loc, v in self.lp if shape(v)}
        self.shape_q = {loc: shape(v) for loc, v in self.lq if shape(v)}
        self.constants_p = {loc: frozenset((value(v),)) for loc, v in self.lp if is_const(v)}
        self.constants_q = {loc: frozenset((value(v),)) for loc, v in self.lq if is_const(v)}
        self.taints = self._taints(self.pairs)

    def _taints(self, pairs) -> dict:
        """{pair: the assumptions its two values contain}, for the pairs that rest on any."""
        if not self.assumed:
            return {}
        held_p, held_q = dict(self.lp), dict(self.lq)
        out = {}
        for pair in pairs:
            found = uses([held_p[pair[0]], held_q[pair[1]]], self.assumed)
            if found:
                out[pair] = frozenset(found)
        return out

    def way(self, sets: dict):
        """The agreement on a way out whose value sets are SETS."""
        pairs, taints = self.pairs, self.taints
        set_p, set_q = self.constants_p, self.constants_q
        if sets:
            # (locations whose values are one for every value of a set: i + 1 and (short)(i + 1) for i in 0..3)
            paired_p = {lp for lp, _, _ in pairs}
            paired_q = {lq for _, lq, _ in pairs}
            by_signature = collections.defaultdict(list)
            for loc, v in self.lq:
                if loc not in paired_q and (loc[0] == "r" or loc[2] == 32):
                    found = signature(v, sets)
                    if found:
                        by_signature[found].append(loc)
            if by_signature:
                more = {
                    (lp, lq, 0)
                    for lp, v in self.lp
                    if lp not in paired_p and (lp[0] == "r" or lp[2] == 32)
                    for lq in by_signature.get(signature(v, sets) or (), ())
                }
                pairs = pairs | more
                taints = self.taints | self._taints(more)
            set_p, set_q = {}, {}
            for held, out in ((self.lp, set_p), (self.lq, set_q)):
                for loc, v in held:
                    found = values_of(v, sets)
                    if found is not None and len(found) <= SET_LIMIT:
                        out[loc] = found
        return pairs, self.known_p, self.known_q, self.shape_p, self.shape_q, set_p, set_q, taints


def shape(v: E) -> tuple[str, int] | None:
    """("sext"|"zext", bits) when V is a narrower value extended."""
    if v.op in ("sext", "zext"):
        return v.op, v.args[0].w
    return None


def merge(ways):
    ways = list(ways)
    pairs = frozenset.intersection(*(w[0] for w in ways))
    out = [pairs]
    for side in (1, 2, 3, 4):
        # (the location and value pairs every way has)
        common = ways[0][side].items()
        for w in ways[1:]:
            common = common & w[side].items()
        out.append(dict(common))
    for side in (5, 6):
        # (a location's values on every way in: their sets' union, while it stays small)
        locations = ways[0][side].keys()
        for w in ways[1:]:
            locations = locations & w[side].keys()
        joined = {}
        for loc in locations:
            union = frozenset().union(*(w[side][loc] for w in ways))
            if len(union) <= SET_LIMIT:
                joined[loc] = union
        out.append(joined)
    # (the assumptions each pair kept rests on, on any way in)
    taints = collections.defaultdict(frozenset)
    for w in ways:
        for pair, found in w[7].items():
            if pair in pairs:
                taints[pair] |= found
    out.append(dict(taints))
    return tuple(out)


def value_in(cases: frozenset, n: int) -> bool:
    return all(0 <= c < n for c in cases)


def start_states(point: Point, agreed, mem: E) -> tuple[State, State]:
    pairs, known_p, known_q, shape_p, shape_q, set_p, set_q, taints = agreed
    # (groups of locations holding one value, or one value and constants more: node -> (parent, value - parent's))
    parent: dict = {}

    def find(x):
        d = 0
        while x in parent:
            x, step = parent[x]
            d += step
        return x, d & 0xFFFFFFFF

    for lp, lq, d in sorted(pairs, key=lambda x: x[2] != 0):
        (a, da), (b, db) = find(("P", lp)), find(("Q", lq))
        if a != b:
            parent[a] = (b, db + d - da)  # (P's value is Q's plus d)
    groups = collections.defaultdict(list)
    for node in {("P", lp) for lp, _, _ in pairs} | {("Q", lq) for _, lq, _ in pairs}:
        root, d = find(node)
        groups[root].append((node, d))
    # (a group's new leaf carries the assumptions any of its pairs rests on: prove(uninitialized=True) names them)
    tainted = collections.defaultdict(frozenset)
    for (lp, lq, d), found in taints.items():
        tainted[find(("P", lp))[0]] |= found
    values = {}
    for n, (root, held) in enumerate(sorted(groups.items(), key=lambda g: repr(sorted(g[1])))):
        members = [node for node, _ in held]
        leaf = (point.index, n, tuple(sorted(tainted[root]))) if tainted.get(root) else (point.index, n)
        if any(d for _, d in held):
            v = mk("in", 32, *leaf)
            for node, d in held:
                values[node] = add(32, v, const(32, d))
            continue
        knowns = {(known_p if side == "P" else known_q).get(loc) for side, loc in members}
        width = members[0][1][2]
        shapes = {(shape_p if side == "P" else shape_q).get(loc) for side, loc in members}
        if len(knowns) == 1 and None not in knowns:
            v = knowns.pop()
        elif len(shapes) == 1 and None not in shapes:
            # (a value extended from a narrower one on every way in stays one)
            kind, bits = shapes.pop()
            v = (sext if kind == "sext" else zext)(width, mk("in", bits, *leaf))
        else:
            v = mk("in", width, *leaf)
        for node in members:
            values[node] = v
    states = []
    for side, known, esp in (("P", known_p, point.esp[0]), ("Q", known_q, point.esp[1])):
        regs, slots = {}, {}
        for r in REGISTERS:
            regs[r] = undef(32, f"{r} at a join")
            for n in (8, 16, 32):
                v = values.get((side, ("r", r, n)), known.get(("r", r, n)))
                if v is not None:
                    regs[r] = insert(regs[r], 0, v)
        # (each slot the widest value a location starting there has, those overlapping one already taken left)
        held = {loc for s, loc in values if s == side} | set(known)
        taken: set[int] = set()
        for loc in sorted((loc for loc in held if loc[0] == "s"), key=lambda loc: (loc[1], -loc[2])):
            k, n = loc[1], loc[2] // 8
            if any(b in taken for b in range(k, k + n)):
                continue
            slots[k] = values.get((side, loc), known.get(loc))
            taken.update(range(k, k + n))
        states.append(State(regs, slots, esp, mem))
    # (the value sets: each location's, on the value it starts with; one set for the two functions' shared values)
    sets: dict = {}
    for state, held in ((states[0], set_p), (states[1], set_q)):
        for loc, found in held.items():
            if loc[0] == "r":
                v = extract(state.regs[loc[1]], 0, loc[2])
            else:
                v = state.slots.get(loc[1])
                if v is None or v.w != loc[2]:
                    continue
            if not is_const(v) and v.op != "undef":
                sets[v] = sets[v] & found if v in sets else found
    # (a part of a value with a set is known from it: one set per value)
    sets = {v: found for v, found in sets.items() if not (v.op == "ext" and v.args[0] in sets)}
    for state in states:
        state.sets = sets
    return states[0], states[1]


def local_pairs(p: Code, q: Code) -> dict[int, int]:
    """{compiled entry offset: original's} of the four-byte stack words the aligned code of the two functions uses
    at the same places (one to one, the most common pairing first): where each keeps a local."""

    def slots(code: Code):
        out = []
        for a in sorted(code.instructions):
            i = code.instructions[a]
            k = None
            for op in i.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_ESP and a in code.esp and not op.mem.index:
                    k = code.esp[a] + op.mem.disp
            out.append((i.mnemonic, k))
        return out

    sp, sq = slots(p), slots(q)
    votes = collections.Counter()
    matcher = difflib.SequenceMatcher(None, [m for m, _ in sp], [m for m, _ in sq], autojunk=False)
    for block in matcher.get_matching_blocks():
        for n in range(block.size):
            kp, kq = sp[block.a + n][1], sq[block.b + n][1]
            if kp is not None and kq is not None and kp < 0 and kq < 0 and kp % 4 == kq % 4:
                votes[kp & ~3, kq & ~3] += 1
    out: dict[int, int] = {}
    for (kp, kq), _ in votes.most_common():
        if kp not in out and kq not in out.values():
            out[kp] = kq
    return out


def uses(expressions, assumed: dict[E, tuple[int, int]]) -> set[tuple[int, int]]:
    """The assumptions (ASSUMED: leaf -> what it stands for) the expressions contain, those a join's new leaves carry
    included."""
    found, seen = set(), set()
    pending = [e for e in expressions if e is not None]
    while pending:
        e = pending.pop()
        if id(e) in seen or e.local is False and e not in assumed and not any(isinstance(a, E) for a in e.args):
            continue
        seen.add(id(e))
        if e in assumed:
            found.add(assumed[e])
        if e.op == "in" and len(e.args) == 3:
            found.update(e.args[2])
        pending += [a for a in e.args if isinstance(a, E)]
    return found


def frame_pairs(p: Code, q: Code) -> tuple[dict[int, E], dict[int, E]]:
    """The two functions' address-taken stack objects as address expressions: an object of P and one of Q are one
    object when the aligned code takes their addresses at the same places; the arguments are where they are."""

    def leas(code: Code):
        out = []
        for a in sorted(code.instructions):
            i = code.instructions[a]
            k = None
            for op in i.operands:
                if op.type == x86.X86_OP_MEM and op.mem.base == x86.X86_REG_ESP and a in code.esp:
                    k = code.region(code.esp[a] + op.mem.disp)
            out.append((i.mnemonic, k))
        return out

    lp, lq = leas(p), leas(q)
    votes = collections.Counter()
    matcher = difflib.SequenceMatcher(None, [m for m, _ in lp], [m for m, _ in lq], autojunk=False)
    for block in matcher.get_matching_blocks():
        for n in range(block.size):
            kp, kq = lp[block.a + n][1], lq[block.b + n][1]
            if kp is not None and kq is not None:
                votes[kp, kq] += 1
    pairing: dict[int, int] = {}
    taken: set[int] = set()
    for (kp, kq), _ in votes.most_common():
        if kp not in pairing and kq not in taken:
            pairing[kp] = kq
            taken.add(kq)
    fp = {b: mk("frame", 32, pairing[b]) if b in pairing else mk("frame", 32, "compiled", b) for b in p.bases}
    fq = {b: mk("frame", 32, b) for b in q.bases}
    for b in p.bases:
        if b >= 4 and pairing.get(b) != b:
            raise Differs(f"the address of argument {b:#x} is taken where the original takes {pairing.get(b)}")
    return fp, fq


@dataclass
class Result:
    proved: bool
    reason: str = ""
    points: int = 0
    # (with uninitialized=True: the locals, (compiled, original) entry offsets, whose values before they are set the
    # proof compared as one)
    uninitialized: tuple[tuple[int, int], ...] = ()
    # (with DISJOINT: the indexes of the pairs the proof used)
    disjoint: tuple[int, ...] = ()


def prove(
    p: Code,
    q: Code,
    returns: int = 32,
    trace=None,
    noreturn=frozenset(),
    parameters=None,
    callees=None,
    uninitialized: bool = False,
    splits: int = 1,
    disjoint=(),
) -> Result:
    """Whether P (compiled) computes what Q (the original's) does. RETURNS: the bits of the return value (0 for
    void; 64 for a value in edx:eax). DISJOINT: pairs of P's instruction addresses whose memory accesses are assumed
    never to overlap (the first runs before the second): an assumption the caller argues, which the result names
    where the proof uses it."""
    _deadline[0] = time.monotonic() + SECONDS
    _interned.clear()
    _interned.update(
        ((e.op, e.w, e.args), e) for e in (TRUE, FALSE, SP)
    )  # (the module's own expressions stay the ones)
    _keys_cache.clear()
    _assumed_disjoint.clear()
    _disjoint_used.clear()
    try:
        for pair in disjoint:
            for a in pair:
                i = p.instructions.get(a)
                if i is None or not any(o.type == x86.X86_OP_MEM for o in i.operands) or i.mnemonic == "lea":
                    raise Unsupported(
                        f"an assumption names {a:#x}, not an instruction of {p.name} that accesses memory"
                    )
        fp, fq = frame_pairs(p, q)
        mp = Machine(p, fp, noreturn, parameters, callees, disjoint)
        mq = Machine(q, fq, noreturn, parameters, callees)
        entry = {("r", r, 32): mk("caller", 32, r) for r in REGISTERS}
        entry["s", 0, 32] = mk("caller", 32, "return")  # (the return address, which a return must find in place)
        for k in range(4, max(p.arguments, q.arguments), 4):
            entry["s", k, 32] = mk("arg", 32, k)
        pairs = frozenset((loc, loc, 0) for loc in entry)
        # (what the frame holds before the function writes it: the caller's leftovers, the same for both, so a local
        # read before it is set is one value where both functions keep it at one place)
        known_p, known_q = dict(entry), dict(entry)
        renamed = local_pairs(p, q) if uninitialized else {}
        for k in range(min(p.frame_bottom, q.frame_bottom) & ~3, 0, 4):
            known_q["s", k, 32] = mk("initial", 32, k)
            if k in renamed:
                # (with UNINITIALIZED: a compiled local's value before it is set taken as the original's local's at
                # the place the aligned code keeps it, an assumption the result names where the proof uses it)
                known_p["s", k, 32] = mk("initial", 32, renamed[k])
            elif k in renamed.values():
                known_p["s", k, 32] = mk("initial", 32, "compiled", k)
            else:
                known_p["s", k, 32] = mk("initial", 32, k)
        assumed = {mk("initial", 32, kq): (kp, kq) for kp, kq in renamed.items() if kp != kq}
        used: set[tuple[int, int]] = set()
        points: dict[tuple, Point] = {}
        flag_variants: dict[tuple[int, int], list] = {}
        first = points[p.address, q.address, None] = Point(p.address, q.address, 0, (0, 0))
        first.ways["entry"] = (pairs, known_p, known_q, {}, {}, {}, {}, {})

        def successors_of(point: Point, ep: End, eq: End, path: int):
            """Compare two paired segment ends (the PATHth a point's run reaches), and queue the pairs of points their
            ways out lead to."""
            where = f"at {ep.at:#x} (compiled) and {eq.at:#x} (original)"

            if ep.kind != eq.kind:
                raise Differs(f"one ends in a {ep.kind}, the other in a {eq.kind} {where}")
            if not alike(ep.state.mem, eq.state.mem, ep.state.sets):
                raise Differs(f"memory differs {where}: {difference(ep.state.mem, eq.state.mem)}")
            if assumed:
                compared = [ep.state.mem, ep.condition, ep.index, ep.state.regs["eax"], ep.state.regs["edx"]]
                used.update(uses(compared, assumed))
            if ep.kind == "ret":
                check_return(ep, eq, where, returns, ep.state.sets)
                return
            if ep.kind == "noreturn":
                return  # (the same call, its memory and arguments compared with the memory)
            sets = ep.state.sets
            if ep.kind == "branch":
                same = same_condition(ep.condition, eq.condition, sets)
                if same == "same":
                    successors = list(zip(ep.targets, eq.targets))
                elif same == "negated":
                    successors = list(zip(ep.targets, reversed(eq.targets)))
                else:
                    raise Differs(f"branch conditions differ {where}: {ep.condition} / {eq.condition}")
                holds = [ep.condition, negate(ep.condition)]
            else:
                if not alike(ep.index, eq.index, sets):
                    raise Differs(f"jump tables are indexed by different values {where}")
                # (the original's table is read while its words point into the function, up to its next table; one of
                # another length would leave the index unbounded in the shorter one: the model has no ranges)
                if len(ep.targets) > len(eq.targets):
                    raise Differs(f"jump tables of {len(ep.targets)} and {len(eq.targets)} entries {where}")
                if len(ep.targets) < len(eq.targets):
                    raise Unsupported(
                        f"jump tables of {len(ep.targets)} and {len(eq.targets)} entries {where}: the index is not"
                        " shown to stay within the compiled one"
                    )
                successors = list(zip(ep.targets, eq.targets))
                cases = values_of(ep.index, sets)
                if cases is not None and not value_in(cases, len(successors)):
                    raise Unsupported(f"a jump table indexed past its end {where}")
                holds = [equal(ep.index, const(ep.index.w, n)) for n in range(len(successors))]
            agreed_at_end = Agreement(ep.state, eq.state, assumed)
            for n, (tp, tq) in enumerate(successors):
                # (the sets that hold on this way; a way they say no value takes is not taken)
                refined = refine(sets, holds[n]) if sets or holds[n].op == "eq" else sets
                if refined is None:
                    continue
                way = agreed_at_end.way(refined)
                esp = (ep.state.esp, eq.state.esp)
                # (ways in with other values of the function's flags are kept apart, a few per pair of addresses:
                # a flag tested twice, with a variable set only when it is, is then never read unset)
                flags = flags_of(way)
                variants = flag_variants.setdefault((tp, tq), [])
                if flags not in variants:
                    if len(variants) < splits:
                        variants.append(flags)
                    else:
                        flags = None
                nxt = points.get((tp, tq, flags))
                if nxt is None:
                    nxt = points[tp, tq, flags] = Point(tp, tq, len(points), esp)
                elif nxt.esp != esp:
                    raise Differs(f"the stack pointers at {tp:#x} and {tq:#x} differ by way")
                nxt.ways[point.index, path, n] = way
                if nxt.index not in queued:
                    queued.add(nxt.index)
                    heapq.heappush(work, (nxt.index, nxt))

        # (the points to run, earliest found first, each once however often its ways change: a point then mostly runs
        # after the ways into it have settled, not once for each of them)
        work = [(0, first)]
        queued = {0}
        runs = 0
        while work:
            _, point = heapq.heappop(work)
            queued.discard(point.index)
            agreed = merge(point.ways.values())
            if point.runs >= WIDEN:
                # (a point run this often has sets still growing, a loop's: it keeps none, so the loop settles)
                agreed = agreed[:5] + ({}, {}) + agreed[7:]
            if agreed == point.done:
                continue
            point.done = agreed
            point.runs += 1
            runs += 1
            if runs > 100 * len(points) + 1000:
                raise Unsupported("no fixpoint")
            check_budget()
            sp, sq = start_states(point, agreed, mk("mem0", 0, point.index))
            if trace:
                print(f"point {point.index}: {point.p:#x} / {point.q:#x}, {len(point.ways)} ways in", file=trace)
                for side, st in (("P", sp), ("Q", sq)):
                    print(f"  {side} slots {sorted((hex(k), v.w) for k, v in st.slots.items())}", file=trace)
                print(f"  sets {[(repr(v)[:60], sorted(found)[:8]) for v, found in sp.sets.items()]}", file=trace)
            for path, (ep, eq) in enumerate(reconcile(mp, mq, mp.run(sp, point.p), mq.run(sq, point.q))):
                successors_of(point, ep, eq, path)
        return Result(
            True, points=len(points), uninitialized=tuple(sorted(used)), disjoint=tuple(sorted(_disjoint_used))
        )
    except (Differs, Unsupported) as e:
        return Result(False, str(e))
    except RecursionError:
        # (an expression deeper than Python's stack: a loop's stores inside one segment)
        return Result(False, "an expression too deep for the model")
    except MemoryError:
        _interned.clear()
        return Result(False, "out of memory")


def _settled(ep: End, eq: End) -> bool:
    """Whether two segment ends are paired as they are: one kind, and branches on one condition."""
    if ep.kind != eq.kind:
        return False
    if ep.kind == "branch":
        return same_condition(ep.condition, eq.condition, ep.state.sets) is not None
    return True


def _decided(e: End, sets: dict) -> int | None:
    """The one way out of a branch or jump table that SETS leave, else None."""
    if e.kind == "branch":
        d = decide(e.condition, sets)
        return e.targets[0] if d is TRUE else e.targets[1] if d is FALSE else None
    if e.kind == "switch":
        found = values_of(e.index, sets)
        if found is not None and len(found) == 1 and min(found) < len(e.targets):
            return e.targets[min(found)]
    return None


def _ways(e: End, sets: dict) -> list[tuple[int, E]] | None:
    """A branch's or a jump table's ways out, each with the condition that takes it; None for a jump table whose index
    has no set."""
    if e.kind == "branch":
        return [(e.targets[0], e.condition), (e.targets[1], negate(e.condition))]
    found = values_of(e.index, sets)
    if found is None or len(found) > SET_LIMIT:
        return None
    if not value_in(found, len(e.targets)):
        raise Unsupported(f"a jump table indexed past its end at {e.at:#x}")
    return [(e.targets[n], equal(e.index, const(e.index.w, n))) for n in sorted(found)]


def reconcile(mp: Machine, mq: Machine, ep: End, eq: End, path=(), depth=0):
    """Pairs of segment ends the two codes reach from ends EP and EQ that branch differently: a side whose way out the
    sets decide runs on alone; otherwise the original's branch (or the compiled one's, or a jump table on an index of a
    few values) is split, each way's condition joining PATH, and that side runs on under it. Ends already paired, or
    still unpaired after a few steps, come out as they are."""
    if depth >= RECONCILE or _settled(ep, eq):
        yield ep, eq
        return
    sets = ep.state.sets
    for e, m, first in ((ep, mp, True), (eq, mq, False)):
        if e.kind in ("branch", "switch") and (t := _decided(e, sets)) is not None:
            moved = m.run(e.state.copy(), t)
            moved.state.sets = sets
            yield from reconcile(mp, mq, moved if first else ep, eq if first else moved, path, depth + 1)
            return
    for e, m, first in ((eq, mq, False), (ep, mp, True)):
        if e.kind not in ("branch", "switch") or (ways := _ways(e, sets)) is None:
            continue
        for t, c in ways:
            refined = path_sets(sets, (*path, c))
            if refined is None:
                continue
            s = e.state.copy()
            s.sets = refined
            moved = m.run(s, t)
            moved.state.sets = refined
            other = ep if not first else eq
            other = End(
                other.kind, other.at, other.state.copy(), other.condition, other.targets, other.index, other.pop
            )
            other.state.sets = refined
            pair = (moved, other) if first else (other, moved)
            yield from reconcile(mp, mq, *pair, (*path, c), depth + 1)
        return
    yield ep, eq


# (how many steps one side may run alone, or split, to meet the other's branching)
RECONCILE = 12


# (the most memory one proof's process may hold, and the longest a proof may run: the largest of 2.6's, adjustforward,
# takes half a gigabyte and under a minute)
MEMORY = 2 << 30
SECONDS = 120
_deadline = [0.0]


def check_budget():
    if resident() > MEMORY:
        raise Unsupported(f"more than {MEMORY >> 30} GB of expressions (no fixpoint in sight)")
    if time.monotonic() > _deadline[0]:
        raise Unsupported(f"more than {SECONDS} seconds (no fixpoint in sight)")


def resident() -> int:
    """This process's resident memory in bytes (0 where /proc is not there)."""
    try:
        with open("/proc/self/statm") as f:
            return int(f.read().split()[1]) * 4096
    except (OSError, IndexError, ValueError):
        return 0


# (the variants of a pair of addresses kept apart by their flags' values, at most, when a proof splits paths: it
# multiplies the work, so proofs run without it first)
SPLITS = 4


def flags_of(way) -> frozenset:
    """The compiled function's flags on a way: its variables (stack slots and callee-saved registers) holding 0 or
    1, and those values."""
    known_p = way[1]
    return frozenset(
        (loc, value(v))
        for loc, v in known_p.items()
        if is_const(v) and value(v) in (0, 1) and (loc[0] == "s" or loc[1] in CALLEE_SAVED) and loc[2] in (8, 32)
    )


def difference(a: E, b: E) -> str:
    """Where two expressions first differ: the innermost pair of differing parts with their operation in common."""
    path = []
    while a.op == b.op and len(a.args) == len(b.args):
        pairs = [(x, y) for x, y in zip(a.args, b.args) if x is not y and x != y]
        # (the first part that differs: a call's memory first, so the earliest of a chain of effects)
        if not pairs or not isinstance(pairs[0][0], E) or not isinstance(pairs[0][1], E):
            break
        path.append(a.op)
        a, b = pairs[0]
    return f"{'/'.join(path[-4:])}: {a!r} / {b!r}"


def check_return(ep: End, eq: End, where: str, returns: int, sets: dict):
    for end, side in ((ep, "compiled"), (eq, "original")):
        if end.state.esp != 0:
            raise Differs(f"the {side} code returns with the stack unbalanced {where}")
        if end.state.slots.get(0) is not mk("caller", 32, "return"):
            raise Differs(f"the {side} code returns without its return address in place {where}")
        for r in CALLEE_SAVED:
            if end.state.regs[r] is not mk("caller", 32, r):
                raise Differs(f"the {side} code returns without the caller's {r} {where}")
    if ep.pop != eq.pop:
        raise Differs(f"ret {ep.pop} and ret {eq.pop} {where}")
    for r, lo in (("eax", 0), ("edx", 32)):
        n = min(returns - lo, 32)
        if n > 0:
            vp, vq = extract(ep.state.regs[r], 0, n), extract(eq.state.regs[r], 0, n)
            if not alike(vp, vq, sets):
                raise Differs(f"return values differ {where}: {vp} / {vq}")


# ------------------------------------------------------------------------------------------- the functions' codes


def _holds(pe: PEFile, address: int, data: bytes) -> bool:
    try:
        return pe.read(address, len(data)) == data
    except ValueError:
        return False


def original_code(pe: PEFile, name: str, address: int, size: int, constants) -> Code:
    return Code(name, address, pe.read(address, size), pe, constants=constants)


def codes(version, row, object_path) -> tuple[Code, Code]:
    """ROW's compiled code (from the object at OBJECT_PATH, with the rest of its source's functions placing its data
    the way the comparison does) and the original's."""
    from . import verdict
    from .coff import CoffError
    from .pe import original

    pe = original(version)
    table = verdict.rows(version)
    obj = Object.read(object_path)
    unit = verdict._Unit(pe, obj, verdict.addresses(version, table), pe.base_relocations)
    found = []
    for r in table:
        if r.source == row.source:
            try:
                symbol = unit.find_function(r)
            except CoffError:
                continue
            found.append((r, symbol, *function_extent(obj, symbol)))
    unit.solve(found)
    p, constants = compiled_code(pe, unit, row, obj)
    return p, original_code(pe, row.name, row.address, row.size, constants)


def compiled_code(pe: PEFile, unit, row, obj: Object) -> tuple[Code, list[tuple[int, int]]]:
    """ROW's function in OBJ, its references resolved the way the comparison resolves them (UNIT, a verdict._Unit
    already solved), placed at the original's address. A literal nothing places is placed where the original's
    aligned instruction refers, when the original has its bytes there; a jump table of the function's own is read
    from the object. Returns the code and the compiler-generated constants it refers to ([start, end) addresses)."""
    from .verdict import _aligned_sites

    symbol = unit.find_function(row)
    section, start, end = function_extent(obj, symbol)
    body = bytearray(section.data[start:end])
    aligned = _aligned_sites(
        section,
        start,
        end,
        pe.read(row.address, row.size),
        [f - row.address for f in unit.fixups if row.address <= f < row.address + row.size],
    )
    referenced = {pe.word(f) for f in unit.fixups if row.address <= f < row.address + row.size - 3}
    tables: dict[int, list[int]] = {}
    constants: list[tuple[int, int]] = []
    statics: dict[tuple[int, int], int] = {}
    assumed: dict[str, int] = {}
    fake = 0xFFF00000
    for r in section.relocations:
        if not start <= r.offset < end:
            continue
        local = r.offset - start
        target = obj.symbol(r.symbol)
        addend = struct.unpack_from("<I", body, local)[0]
        address = unit.resolve(target, addend, (section, start, row.address))
        literal = target.defined and c_name(target.name).startswith("@")
        if target.defined and unit.named_address(target) is None and target.section != section.number:
            key, delta = unit.datum(target, addend)
            data_section = obj.section(key[0])
            datum_end = unit.datum_end(data_section, key[1])
            inner = [x for x in data_section.relocations if key[1] <= x.offset < datum_end]
            if not data_section.code and inner and all(obj.symbol(x.symbol).section == section.number for x in inner):
                # (a jump table of this function's: its entries in the compiled code's own layout)
                entries = []
                for x in sorted(inner, key=lambda x: x.offset):
                    word = struct.unpack_from("<I", data_section.data, x.offset)[0]
                    entries.append(unit.resolve(obj.symbol(x.symbol), word, (section, start, row.address)))
                address = fake + delta
                tables[fake] = entries
                fake += 0x10000
            elif literal and not inner:
                data = data_section.data[key[1] : datum_end]
                if address is None and local in aligned:
                    candidate = (unit.target_of(row, 0, aligned[local], r.kind) - delta) & 0xFFFFFFFF
                    try:
                        if pe.read(candidate, len(data)) == data:
                            address = (candidate + delta) & 0xFFFFFFFF
                    except ValueError:
                        pass
                if address is None:
                    # (else the one address the original's code refers to that has the literal's bytes)
                    found = {w for w in referenced if _holds(pe, w, data)}
                    if len(found) == 1:
                        address = (found.pop() + delta) & 0xFFFFFFFF
                if address is not None:
                    base = (address - delta) & 0xFFFFFFFF
                    try:
                        if pe.read(base, len(data)) != data:
                            raise Differs(f"{target.name}'s bytes are not the original's at {base:#x}")
                    except ValueError:
                        raise Differs(f"{target.name} placed outside the executable") from None
                    constants.append((base, base + len(data)))
            elif address is None:
                # (a static nothing places: where the section's placed data say it is, the section laid out as in the
                # object, else where the original's aligned instruction refers; one static to one place)
                bases = {place - offset for (number, offset), place in unit.places.items() if number == key[0]}
                if len(bases) == 1:
                    candidate = (bases.pop() + key[1]) & 0xFFFFFFFF
                elif local in aligned:
                    candidate = (unit.target_of(row, 0, aligned[local], r.kind) - delta) & 0xFFFFFFFF
                else:
                    raise Differs(f"{target.name} is not placed or bound")
                if statics.setdefault(key, candidate) != candidate or list(statics.values()).count(candidate) > 1:
                    raise Differs(f"{target.name} would be at two places, or two statics at one")
                assumed[f"{target.name}+{key[1]:#x}" if target.aux else target.name] = candidate
                address = (candidate + delta) & 0xFFFFFFFF
        if address is None:
            raise Differs(f"{target.name} is not placed or bound")
        if r.kind == DIR32:
            v = address
        elif r.kind == DIR32NB:
            v = address - pe.image_base
        elif r.kind == REL32:
            v = address - (row.address + local + 4)
        else:
            raise Unsupported(f"relocation kind {r.kind}")
        struct.pack_into("<I", body, local, v & 0xFFFFFFFF)
    code = Code(row.name + " (compiled)", row.address, bytes(body), pe, tables, constants)
    code.assumed = assumed
    return code, constants
