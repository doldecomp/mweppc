#!/usr/bin/env python3
"""python tools/fn.py COMMAND ...: one function: the original's code, the compiled code against it, and trials of
other source for it.

  show VERSION NAME | show VERSION --address ADDRESS [--size N]
      the original's code, with the addresses it calls or reads named (functions.json, then symbols.txt)
  diff NAME [--version V] [--built]
      compile NAME's source outside the build (--built: take the build's object) and show the code against the
      original's, instruction by instruction ('=' the same, '~' the same instruction with other operands, '-' only the
      compiled, '+' only the original), with the verdict and its reason. Addresses print as names; a line is '='
      only when it refers to the same globals and callees ('?' is a reference the comparison cannot resolve). Exits 1
      when the function is not exact.
  variants NAME FILE [--version V] [--write VARIANT]
      compile alternative definitions of NAME (FILE: whole definitions, each headed by a line `//// VARIANT`; text
      before the first header is ignored) in parallel, each in place of the definition, and judge each; a variant may
      add declarations before the function. Other functions of the unit whose verdict a variant changes are listed
      (inlining). --write VARIANT puts that one in the source.
  decl-order NAME [--version V] [--swaps] [--dry-run]
      search the order of NAME's leading local declarations for the one closest to the original, and write it (unless
      --dry-run). Greedy: each pass tries every declaration at every place (with --swaps, every pair exchanged too)
      and keeps the best, until nothing helps or the function is exact. MWCC allocates registers in declaration order,
      so this settles "the same code in other registers". Declarations that are static or initialized by a call keep
      their places.

NAME is a row of config/VERSION/functions.json; its source is the row's. Everything compiles outside the build
(tools/mw/isolated.py: the source's own command line, its own file name), in parallel, judged by the comparison's rule
(tools/mw/verdict.py); nothing in build/ is written, so it runs beside ninja and other tools. A write fails when the
source changed while the command ran.
"""

import argparse
import bisect
import difflib
import re
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, csrc, formats, isolated, project, verdict
from mw.pe import original

MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_ADDRESS = re.compile(r"0x[0-9a-f]{6,}")


def find_row(version, name) -> verdict.Row:
    found = [r for r in verdict.rows(version) if r.name == name and r.symbol == name]
    if not found:
        raise project.Error(f"no function {name} in config/{version}/functions.json")
    return found[0]


def source_row(version, name) -> verdict.Row:
    row = find_row(version, name)
    if not row.source:
        raise project.Error(f"{name} has no source in config/{version}/functions.json")
    return row


def disassemble(code: bytes, address: int) -> list[tuple[int, str, str]]:
    return [(i.address - address, i.mnemonic, i.op_str) for i in MD.disasm(code, address)]


# ----------------------------------------------------------------------------------------------------------- show


def namer(version, rows=None):
    """A function naming an address: a row's or symbols.txt's name, `name+0xN` inside a symbol, or the address."""
    rows = verdict.rows(version) if rows is None else rows
    names, sizes = {}, {}
    for s in formats.read_symbols(version.config / "symbols.txt"):
        names.setdefault(s.address, s.name)
        sizes.setdefault(s.address, s.size or 0)
    names.update({r.address: r.name for r in rows})
    sizes.update({r.address: r.size for r in rows})
    starts = sorted(names)

    def name(address: int) -> str:
        if address == verdict.UNRESOLVED:
            return "?"
        if address in names:
            return names[address]
        i = bisect.bisect_right(starts, address) - 1
        if i >= 0 and address < starts[i] + max(sizes[starts[i]], 1):
            return f"{names[starts[i]]}+{address - starts[i]:#x}"
        return f"{address:#x}"

    return name


def show(args):
    v = project.version(args.version)
    rows = verdict.rows(v)
    name = namer(v, rows)
    if args.address is not None:
        try:
            address = int(args.address, 16)
        except ValueError:
            raise project.Error(f"--address takes a hexadecimal address, not {args.address}") from None
        row = next((r for r in rows if r.address == address), None)
        size = args.size or (row.size if row else None)
        if size is None:
            raise project.Error(f"no row at {address:#x}: give --size")
    else:
        row = find_row(v, args.name)
        address, size = row.address, args.size or row.size
    for i in MD.disasm(original(v).read(address, size), address):
        operands = _ADDRESS.sub(lambda m: name(int(m[0], 16)), i.op_str)
        print(f"{i.address:x}: {i.mnemonic} {operands}")


# ----------------------------------------------------------------------------------------------------------- diff


def side_by_side(left, right, left_name, right_name, address=0, size=0, name=None, same_size=False):
    """Instruction diff. Absolute addresses print by NAME (a branch inside the function [ADDRESS, +SIZE) as `.+0xN`);
    a line is '=' only when its operands, references included, are the same (an unresolved reference, '?', matches
    anything: the verdict's reason says why it is unresolved). A branch inside the function matches any other unless
    SAME_SIZE (both codes one size: then it must go to the same offset)."""
    lo, hi = address, address + size

    def label(m):
        x = int(m[0], 16)
        if lo <= x < hi:
            return f".+{x - lo:#x}"
        return name(x) if name else "ADDR"

    a = [f"{m} {_ADDRESS.sub(label, o)}" for _, m, o in left]
    b = [f"{m} {_ADDRESS.sub(label, o)}" for _, m, o in right]
    shape_a = [f"{m} {_ADDRESS.sub('A', o)}" for _, m, o in left]
    shape_b = [f"{m} {_ADDRESS.sub('A', o)}" for _, m, o in right]
    print(f"    {left_name:<48} {right_name}")
    matcher = difflib.SequenceMatcher(None, [x.split()[0] for x in a], [x.split()[0] for x in b], autojunk=False)
    for tag, i1, i2, j1, j2 in matcher.get_opcodes():
        if tag == "equal":
            for i, j in zip(range(i1, i2), range(j1, j2)):
                same = shape_a[i] == shape_b[j] and verdict.references_agree(left[i][2], right[j][2], lo, hi)
                if same and same_size and a[i] != b[j]:
                    same = False  # (code of one size: a branch inside the function elsewhere is a difference)
                mark = "=" if same else "~"
                print(f"{mark} {left[i][0]:5x} {a[i][:44]:<44} {right[j][0]:5x} {b[j]}")
        else:
            for i in range(i1, i2):
                print(f"- {left[i][0]:5x} {a[i]}")
            for j in range(j1, j2):
                print(f"+ {' ' * 50}{right[j][0]:5x} {b[j]}")


def diff(args) -> bool:
    v = project.version(args.version)
    row = source_row(v, args.name)
    pe = original(v)
    target = pe.read(row.address, row.size)
    with isolated.Workspace() as workspace:
        if args.built:
            obj = build.object_path(v, row.source)
            if not obj.exists():
                raise project.Error(f"{project.relative(obj)} does not exist: run ninja")
        else:
            result = workspace.compile(build.compilation(v, row.source))
            if not result.ok:
                print(result.log[-3000:])
                raise project.Error(f"{row.source} does not compile")
            obj = result.object
        found = next((x for x in verdict.compare_file(v, row.source, obj) if x.address == row.address), None)
        if found is None:
            raise project.Error(f"{row.source}'s object does not define {row.name}")
    state = "EXACT" if found.exact else f"{100 * found.similarity:.1f}% similar: {found.reason}"
    print(f"{row.name} ({row.source}, {row.address:#x}, {row.size} bytes): {state}")
    if found.body is not None:
        side_by_side(
            disassemble(found.body, row.address),
            disassemble(target, row.address),
            "compiled",
            "original",
            row.address,
            max(len(found.body), row.size),
            namer(v),
            same_size=len(found.body) == row.size,
        )
    return found.exact


# ------------------------------------------------------------------------------------------------ trials of source


class Trial:
    """Compiles texts of ROW's source outside the build and judges them."""

    def __init__(self, version, row: verdict.Row, workspace: isolated.Workspace):
        self.version, self.row, self.workspace = version, row, workspace
        self.compilation = build.compilation(version, row.source)
        self.target = original(version).read(row.address, row.size)

    def run(self, text: str):
        """(score: 2 exact, else the similarity, -1 when it does not compile; {address: exact} of the unit's
        functions; the row's verdict or the compiler's log)."""
        result = self.workspace.compile(self.compilation, text)
        if not result.ok:
            return -1.0, {}, " ".join(result.log.split())[-400:]
        verdicts = verdict.compare_file(self.version, self.row.source, result.object, score=False)
        result.object.unlink()
        mine = next(x for x in verdicts if x.address == self.row.address)
        if mine.body is not None and not mine.exact:
            mine.similarity = verdict.similarity(mine.body, self.target, self.row.address)
        return (2.0 if mine.exact else mine.similarity), {x.address: x.exact for x in verdicts}, mine


def describe(score, outcome) -> str:
    if score < 0:
        return f"does not compile: {outcome}"
    if score >= 2:
        return "EXACT"
    return f"{100 * score:.1f} ({outcome.reason})"


def write_source(path: str, before: str, after: str) -> None:
    with project.Transaction() as tx:
        tx.unchanged(path, before)
        tx.write(path, after.encode("latin-1"))


def variants(args):
    v = project.version(args.version)
    row = source_row(v, args.name)
    source = csrc.Source(row.source, version=v)
    d = source.find(row.name)
    text = source.text
    parts = re.split(r"^//// *(.*)\n", Path(args.file).read_text(encoding="latin-1"), flags=re.M)
    candidates = {"(current)": text[d.start : d.end]}
    for name, body in zip(parts[1::2], parts[2::2]):
        name = name.strip()
        if name in candidates:
            raise project.Error(f"{args.file}: two variants named {name}")
        candidates[name] = body.strip("\n")
    if len(candidates) == 1:
        raise project.Error(f"{args.file} has no variants (each headed by a line `//// NAME`)")
    if args.write:
        if args.write not in candidates:
            raise project.Error(f"{args.file} has no variant {args.write}")
        write_source(row.source, text, text[: d.start] + candidates[args.write] + text[d.end :])
        print(f"{row.source}: {row.name} is now {args.write}")
        return
    names = {r.address: r.name for r in verdict.rows(v)}
    with isolated.Workspace() as workspace:
        trial = Trial(v, row, workspace)
        jobs = [text[: d.start] + body + text[d.end :] for body in candidates.values()]
        base = None
        for name, (score, exact, outcome) in zip(candidates, isolated.parallel(trial.run, jobs)):
            if base is None:
                base = exact
            changed = [
                ("+" if exact[a] else "-") + names[a]
                for a in exact
                if a != row.address and a in base and exact[a] != base[a]
            ]
            print(
                f"{name}: {describe(score, outcome)}" + (f"; others: {' '.join(changed)}" if changed else ""),
                flush=True,
            )


def decl_order(args):
    v = project.version(args.version)
    row = source_row(v, args.name)
    source = csrc.Source(row.source, version=v)
    d = source.find(row.name)
    text = source.text
    if len(d.locals) < 2:
        raise project.Error(f"{row.name} starts with {len(d.locals)} declaration statement(s): nothing to order")
    first, last = d.locals[0].start, d.locals[-1].end
    if any(first <= x.start < last for x in source.directives):
        raise project.Error(f"{row.name}: a preprocessor line among its declarations")
    # (each declaration with what follows it on its line: a comment)
    spans = []
    for local in d.locals:
        end = source.line_end(local.end)
        spans.append((local.start, end - 1 if end > local.end else local.end))
    last = spans[-1][1]
    pieces = [text[s:e] for s, e in spans]
    gaps = [text[spans[i][1] : spans[i + 1][0]] for i in range(len(spans) - 1)]
    movable = [i for i, local in enumerate(d.locals) if local.movable]
    pinned = [text[s:e].strip() for (s, e), local in zip(spans, d.locals) if not local.movable]
    if len(movable) < 2:
        raise project.Error(f"{row.name}: fewer than two declarations can move (pinned: {'; '.join(pinned)})")

    def render(order):
        """The source with the movable declarations in ORDER (indices into pieces) in their places."""
        slots = iter(order)
        chosen = [next(slots) if i in movable else i for i in range(len(pieces))]
        middle = "".join(pieces[k] + (gaps[i] if i < len(gaps) else "") for i, k in enumerate(chosen))
        return text[:first] + middle + text[last:]

    scores = {}
    with isolated.Workspace() as workspace:
        trial = Trial(v, row, workspace)

        def score_all(orders):
            todo = [o for o in dict.fromkeys(orders) if o not in scores]
            for order, (score, _, _) in zip(todo, isolated.parallel(trial.run, [render(o) for o in todo])):
                scores[order] = score
            return [scores[o] for o in orders]

        best = tuple(movable)
        start_score = best_score = score_all([best])[0]
        if best_score < 0:
            raise project.Error(f"{row.source} does not compile as it is")
        print(
            f"{row.name}: {describe(best_score, '...') if best_score >= 2 else f'{100 * best_score:.1f}'}"
            + (f" (pinned: {'; '.join(pinned)})" if pinned else "")
        )
        improved = True
        while improved and best_score < 2:
            improved = False
            moves = []
            for k in best:
                rest = [x for x in best if x != k]
                moves.append(
                    (f"{pieces[k].strip()} to #\0", [tuple(rest[:i] + [k] + rest[i:]) for i in range(len(best))])
                )
            if args.swaps:
                for a in range(len(best)):
                    swapped = []
                    for b in range(a + 1, len(best)):
                        c = list(best)
                        c[a], c[b] = c[b], c[a]
                        swapped.append(tuple(c))
                    if swapped:
                        moves.append((f"{pieces[best[a]].strip()} swapped with the #\0 after it", swapped))
            for what, candidates in moves:
                results = score_all(candidates)
                i = max(range(len(results)), key=results.__getitem__)
                if results[i] > best_score:
                    best, best_score, improved = candidates[i], results[i], True
                    print(f"  {100 * min(best_score, 1):.1f} {what.replace(chr(0), str(i))}", flush=True)
                    if best_score >= 2:
                        break
    verdict_text = "EXACT" if best_score >= 2 else f"best {100 * best_score:.1f}"
    if best_score <= start_score:
        print(f"{verdict_text}: no order does better; nothing written")
    elif args.dry_run:
        print(f"{verdict_text} (not written):\n" + render(best)[first : first + len("".join(pieces)) + 200])
    else:
        write_source(row.source, text, render(best))
        print(f"{verdict_text}; written")


def main():
    import signal

    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # (piped into head: stop quietly)
    parser = argparse.ArgumentParser(
        prog="tools/fn.py",
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__[__doc__.index("\n\n") + 2 :],
    )
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--version", help=f"default {project.PRIMARY}")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    p = commands.add_parser("show")
    p.add_argument("version")
    p.add_argument("name", nargs="?")
    p.add_argument("--address")
    p.add_argument("--size", type=int)
    p = commands.add_parser("diff", parents=[common])
    p.add_argument("name")
    p.add_argument("--built", action="store_true", help="the build's object, not a fresh compile")
    p = commands.add_parser("variants", parents=[common])
    p.add_argument("name")
    p.add_argument("file")
    p.add_argument("--write")
    p = commands.add_parser("decl-order", parents=[common])
    p.add_argument("name")
    p.add_argument("--swaps", action="store_true")
    p.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if args.command == "show" and (args.name is None) == (args.address is None):
        parser.error("show takes a NAME or --address")
    try:
        if args.command == "show":
            show(args)
        elif args.command == "diff":
            sys.exit(0 if diff(args) else 1)
        elif args.command == "variants":
            variants(args)
        else:
            decl_order(args)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
