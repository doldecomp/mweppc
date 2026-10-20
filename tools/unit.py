#!/usr/bin/env python3
"""python tools/unit.py COMMAND ...: a source as a whole.

  check SOURCE... [--version V | --all]
      compile sources outside the build and list each function, exact or how close (and why not exact), marking what
      changed since the last build's comparison: "+" newly exact, "-" no longer exact. Exits 1 when a function that
      was exact is not.
  order SOURCE [--address | --deferred] [--version V] [--dry-run]
      put the source's functions that are rows in the original's order: ascending addresses, or descending for a
      unit built -inline auto,deferred (whose functions the binary holds in reverse source order); by default the
      order of how the source is built (its flags, or #pragma defer_codegen), which an option overrides. Each function
      takes the place of another (its comments, and the pragmas and #if group that belong to it alone, go with it);
      the rest of the file stays as it is: functions without a row (inlined statics, dead code), declarations.
      Refused when a function would change #pragma settings or #if branch.
  move DEST NAME... [--version V]
      move functions' definitions into the source DEST and make DEST their source in every version's
      functions.json. Each goes before DEST's first function (a row) that follows it in the original's order:
      ascending addresses, descending when DEST is built deferred. A DEST that does not exist yet is created, built
      like the first function's source (config/sources.json). Declarations the moved code needs are not moved: build
      after and add what DEST lacks (tools/includes.py fix DEST). Refused for a definition inside an #if of more than
      itself; a change of #pragma settings is reported.
  assign SOURCE NAME... [--version V]
      make SOURCE the source of the functions NAME..., for functions newly written there (SOURCE "" unassigns them).
      A name is the same function in every version, so every version's row of that name is assigned, unless
      --version picks one. Every name must be some version's row.

Functions are found with clang (tools/mw/csrc.py). Edits are one transaction: every file is written, or none (a file
that changed while the command ran fails it).
"""

import argparse
import json
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, config, csrc, isolated, project, verdict

# ---------------------------------------------------------------------------------------------------------- check


def check_one(version, source, workspace) -> bool:
    result = workspace.compile(build.compilation(version, source))
    if not result.ok:
        print(f"{version} {source}: does not compile\n{result.log[-3000:]}")
        return False
    verdicts = sorted(verdict.compare_file(version, source, result.object), key=lambda v: v.address)
    before = verdict.last(version)
    lost = False
    lines = []
    for v in verdicts:
        was = before.get(v.address, {}).get("exact")
        mark = " " if was is None or was == v.exact else ("+" if v.exact else "-")
        lost |= mark == "-"
        if v.exact:
            lines.append(f" {mark} EXACT {v.name}")
        else:
            lines.append(f" {mark} {100 * v.similarity:5.1f} {v.name}: {v.reason}")
    print(f"{version} {source}: {sum(v.exact for v in verdicts)} of {len(verdicts)} exact")
    print("\n".join(lines))
    return not lost


def check(args) -> bool:
    versions = list(project.VERSIONS) if args.all else [project.version(args.version)]
    sources = [source_path(s) for s in args.sources]
    with isolated.Workspace() as workspace:
        return all([check_one(v, s, workspace) for v in versions for s in sources])


# ---------------------------------------------------------------------------------------------------------- order


def source_path(name: str) -> str:
    """NAME (any spelling of a path) as config/sources.json names it."""
    path = project.relative(Path(name).resolve())
    if path not in config.sources():
        raise project.Error(f"{path} is not in config/sources.json")
    return path


def deferred(version, source: str, text: str) -> bool:
    flags = build.compilation(version, source).flags
    inline = flags[flags.index("-inline") + 1] if "-inline" in flags else ""
    return "deferred" in inline or re.search(r"^\s*#\s*pragma\s+defer_codegen\s+on\b", text, re.M) is not None


def placed(version, source: csrc.Source) -> list[tuple[int, csrc.Declaration]]:
    """[(address, definition)] of SOURCE's definitions that are rows of its own, in text order."""
    return source.placed({r.name: r.address for r in verdict.rows(version) if r.source == source.path})


def order(args):
    v = project.version(args.version)
    path = source_path(args.source)
    source = csrc.Source(path, version=v)
    functions = placed(v, source)
    built = deferred(v, path, source.text)
    descending = args.deferred or (built and not args.address)
    if descending != built:
        print(f"note: {path} is built {'deferred' if built else 'in source order'}; ordering it the other way")
    text, moved = source.ordered(functions, descending=descending)
    if not moved:
        print(f"{path}: its {len(functions)} functions are in order already")
        return
    if args.dry_run:
        print(f"{path}: {moved} of {len(functions)} functions would move")
        return
    with project.Transaction() as tx:
        tx.unchanged(path, source.text)
        tx.write(path, text.encode("latin-1"))
    print(f"{path}: {moved} of {len(functions)} functions moved")


# ----------------------------------------------------------------------------------------------------------- move


def cut(text: str, spans: list[tuple[int, int]]) -> str:
    """TEXT without SPANS (whole lines), leaving one blank line where one was cut out between others."""
    for start, end in sorted(spans, reverse=True):
        before, after = text[:start], text[end:]
        if not after.strip():
            text = before.rstrip("\n") + "\n" if before.strip() else ""
        elif not before or before.endswith("\n\n"):
            text = before + after.lstrip("\n")
        else:
            text = before + after
    return text


def move(args):
    v = project.version(args.version)
    dest = project.relative(Path(args.dest).resolve())
    if not dest.startswith("src/") or Path(dest).suffix not in (".c", ".cpp"):
        raise project.Error(f"{dest}: a source is a .c or .cpp file under src/")
    rows = {r.name: r for r in verdict.rows(v)}
    sources = {}
    moving = []  # (address, name, extent text, pragma state there)
    for name in dict.fromkeys(args.names):
        row = rows.get(name)
        if row is None or not row.source:
            raise project.Error(f"{name}: no row with a source in config/{v}/functions.json")
        if row.source == dest:
            raise project.Error(f"{name} is in {dest} already (tools/unit.py order reorders a source)")
        source = sources.get(row.source) or sources.setdefault(row.source, csrc.Source(row.source, version=v))
        d = source.find(name)
        start, end = source.extent(d)
        if source.conditional_at(start):
            raise project.Error(
                f"{name}: inside an #if at line {source.line(source.conditional_at(start)[-1])} of "
                f"{row.source} that holds more than it: move it by hand"
            )
        if not source.balanced(start, end):
            raise project.Error(f"{name}: its text changes #pragma settings or #if nesting")
        if source.spelled(d) != d.name:
            print(f"note: {name} is spelled {source.spelled(d)} in {row.source}: the macro that names it stays there")
        moving.append((row.address, name, row.source, (start, end), source.pragma_state(start)))

    table = config.sources()
    with project.Transaction() as tx:
        new = not (project.ROOT / dest).exists()
        if new:
            if dest in table:
                raise project.Error(f"{dest} is in config/sources.json but does not exist")
            target, text, here = None, '#include "compiler/common.h"\n', []
            descending = deferred(v, moving[0][2], "")
        else:
            if dest not in table:
                raise project.Error(f"{dest} is not in config/sources.json")
            target = csrc.Source(dest, version=v)
            text, here = target.text, placed(v, target)
            descending = deferred(v, dest, text)
        # (where each goes: before the first of DEST's functions it precedes in the original's order)
        inserts = []
        for address, name, source, (start, end), state in moving:
            follows = (lambda other: other < address) if descending else (lambda other: other > address)
            before = next((d for other, d in here if follows(other)), None)
            if before:
                at = target.extent(before)[0]
            else:  # (after DEST's last function: a file can end with pragmas for the whole unit)
                at = target.extent(here[-1][1])[1] if here else len(text)
            there = target.pragma_state(at) if target else ()
            if there != state:
                print(f"note: {name}'s #pragma settings change from {csrc.describe(state)} to {csrc.describe(there)}")
            if target and target.conditional_at(at):
                raise project.Error(f"{name} would land inside an #if of {dest}")
            inserts.append((at, -address if descending else address, sources[source].text[start:end]))
        for at, _, piece in sorted(inserts, reverse=True):
            head, tail = text[:at], text[at:].lstrip("\n")
            head = head.rstrip("\n") + "\n\n" if head.strip() else head
            text = head + piece + ("\n" + tail if tail.strip() else "")
        tx.write(dest, text.encode("latin-1"))
        for path, source in sources.items():
            spans = [extent for _, _, s, extent, _ in moving if s == path]
            tx.unchanged(path, source.text)
            tx.write(path, cut(source.text, spans).encode("latin-1"))
        if new:
            raw = json.loads(tx.read(config.SOURCES_PATH))
            # (beside the source it is built like)
            raw = {
                k: e
                for key, value in raw.items()
                for k, e in ([(key, value), (dest, value)] if key == moving[0][2] else [(key, value)])
            }
            tx.write(config.SOURCES_PATH, config.dump_json(raw))
        names = {name for _, name, _, _, _ in moving}
        for version in project.VERSIONS:
            path = version.config / "functions.json"
            table_rows = config.functions(version, tx.read(path))
            for r in table_rows:
                if r.name in names:
                    r.source = dest
            tx.write(path, config.dump_functions(table_rows))
    for _, name, source, _, _ in sorted(moving):
        print(f"{name}: {source} -> {dest}")
    if new:
        print(f"created {dest}, built like {moving[0][2]} (config/sources.json): run configure.py again")


# --------------------------------------------------------------------------------------------------------- assign


def assign(args):
    versions = [project.version(args.version)] if args.version else project.VERSIONS
    if args.source and args.source not in config.sources():
        raise project.Error(f"{args.source} is not in config/sources.json")
    wanted, found = set(args.names), set()
    with project.Transaction() as tx:
        for v in versions:
            path = v.config / "functions.json"
            rows = config.functions(v, tx.read(path))
            count = 0
            for row in rows:
                if row.name in wanted:
                    row.source = args.source or None
                    found.add(row.name)
                    count += 1
            tx.write(path, config.dump_functions(rows))
            print(f"{v}: {count} rows")
        if wanted - found:
            raise project.Error(f"no row named {' '.join(sorted(wanted - found))}")


def main():
    import signal

    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # (piped into head: stop quietly)
    parser = argparse.ArgumentParser(
        prog="tools/unit.py",
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__[__doc__.index("\n\n") + 2 :],
    )
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--version", help=f"default {project.PRIMARY}")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    p = commands.add_parser("check", parents=[common])
    p.add_argument("sources", nargs="+")
    p.add_argument("--all", action="store_true", help="every version")
    p = commands.add_parser("order", parents=[common])
    p.add_argument("source")
    how = p.add_mutually_exclusive_group()
    how.add_argument("--address", action="store_true", help="ascending addresses")
    how.add_argument("--deferred", action="store_true", help="descending addresses")
    p.add_argument("--dry-run", action="store_true")
    p = commands.add_parser("move", parents=[common])
    p.add_argument("dest")
    p.add_argument("names", nargs="+")
    p = commands.add_parser("assign", parents=[common])
    p.add_argument("source")
    p.add_argument("names", nargs="+")
    args = parser.parse_args()
    try:
        if args.command == "check":
            sys.exit(0 if check(args) else 1)
        elif args.command == "order":
            order(args)
        elif args.command == "move":
            move(args)
        else:
            assign(args)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
