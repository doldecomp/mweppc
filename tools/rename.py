#!/usr/bin/env python3

import argparse
import concurrent.futures
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, project, renaming


def report(plan, dry_run):
    if plan.problems:
        sys.exit("nothing renamed:\n" + "\n".join(f"  {p}" for p in plan.problems))
    for s in plan.left:
        print(f"left (a member of that name): {project.relative(s.path)}:{s.line}")
    if not plan.changes:
        sys.exit("error: nothing to rename (no row, binding, symbol, source or code names OLD)")
    if dry_run:
        print("\n".join(plan.changes))
        return
    changed = plan.tx.commit()
    print("\n".join(plan.changes))
    if changed:
        print(f"{len(changed)} files changed")


_DIAGNOSTIC = re.compile(r"^(.+?):(\d+):(\d+): (error|note): (.*)$")
_MISSING = re.compile(r"no member named '(\w+)' in '(?:(?:struct|union) )?(\w+)'")


def diagnostics(compilation):
    result = clang.run(compilation, ["-fsyntax-only", "-ferror-limit=0", "-fno-caret-diagnostics"])
    out = []
    for line in result.stderr.decode("latin-1").splitlines():
        if m := _DIAGNOSTIC.match(line):
            out.append((m[4], m[5], (project.ROOT / m[1]).resolve(), int(m[2]), int(m[3])))
    return out


def spells(where, member):
    path, line, column = where
    try:
        text = path.read_text(encoding="latin-1").split("\n")[line - 1]
    except (OSError, IndexError):
        return False
    return re.match(rf"{re.escape(member)}\b", text[column - 1 :]) is not None


def member_sites(entries, specs):
    with concurrent.futures.ThreadPoolExecutor(os.cpu_count() or 4) as pool:
        results = list(pool.map(diagnostics, entries))
    found = {}
    for diags in results:
        for i, (kind, text, path, line, column) in enumerate(diags):
            m = _MISSING.search(text) if kind == "error" else None
            if not m or (m[2], m[1]) not in specs:
                continue
            candidates = [(path, line, column)]
            for kind2, text2, path2, line2, column2 in diags[i + 1 :]:
                if kind2 != "note" or "expanded from macro" not in text2:
                    break
                candidates.append((path2, line2, column2))
            for where in candidates:
                if spells(where, m[1]):
                    found.setdefault(where, (m[2], m[1]))
                    break
    return found


def apply_members(found, specs, dry_run):
    by_file = {}
    for (path, line, column), key in found.items():
        by_file.setdefault(path, []).append((line, column, key))
    count = 0
    tx = project.Transaction()
    for path, edits in by_file.items():
        lines = tx.read_bytes(path).decode("latin-1").split("\n")
        for line, column, (record, member) in sorted(edits, key=lambda e: (e[0], -e[1])):
            text, i = lines[line - 1], column - 1
            best = None
            for spec_path, new in specs[(record, member)]:
                pattern = re.escape(member) + "".join(rf"\s*\.\s*{re.escape(p)}\b" for p in spec_path[1:])
                m = re.match(pattern + r"(?!\w)", text[i:])
                if m and (best is None or len(spec_path) > best[0]):
                    best = (len(spec_path), m.end(), new)
            if best is None:
                continue
            lines[line - 1] = text[:i] + best[2] + text[i + best[1] :]
            count += 1
            print(f"{project.relative(path)}:{line}: {lines[line - 1].strip()[:110]}")
        tx.write(path, "\n".join(lines).encode("latin-1"))
    if not dry_run:
        tx.commit()
    return count


def rename_members(spec_texts, filter_text, dry_run):
    specs = {}
    for spec in spec_texts:
        old, equals, new = spec.partition("=")
        record, *member_path = old.split(".")
        if not equals or not member_path or not re.fullmatch(r"\w+", record):
            sys.exit(f"error: {spec!r} is not RECORD.OLD=NEW")
        specs.setdefault((record, member_path[0]), []).append((member_path, new))
    entries = [c for c in build.compilations() if filter_text in c.source]
    total = 0
    while True:
        found = member_sites(entries, specs)
        count = apply_members(found, specs, dry_run) if found else 0
        total += count
        if not count or dry_run:
            break
    left = {} if dry_run else member_sites(entries, specs)
    for (path, line, column), (record, member) in sorted(left.items()):
        print(f"left: {project.relative(path)}:{line}:{column}: {record}.{member}")
    print(f"{total} uses renamed" + (f", {len(left)} left" if left else ""))
    if left:
        sys.exit(1)


def main():
    parser = argparse.ArgumentParser(
        prog="tools/rename.py",
        usage="%(prog)s OLD NEW [OLD NEW...] [--force NEW...] [--no-code] [--source SRC] [--dry-run]\n"
        "       %(prog)s --unit OLD NEW [--dry-run]\n"
        "       %(prog)s --member [--filter TEXT] [--dry-run] RECORD.OLD[.MORE]=NEW...",
        description="Rename functions and variables everywhere the repository names them, in every version.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("names", nargs="+", metavar="NAME")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--unit", action="store_true", help="rename a source file")
    mode.add_argument("--member", action="store_true", help="rename struct members at every use")
    parser.add_argument("--filter", default="", metavar="TEXT", help="with --member: only sources whose path has TEXT")
    parser.add_argument("--force", nargs="+", default=[], metavar="NEW", help="rename to NEW though it is in the code")
    parser.add_argument("--no-code", action="store_true", help="leave src/ and include/ alone")
    parser.add_argument("--source", metavar="SRC", help="assign the renamed rows to SRC")
    parser.add_argument("--dry-run", action="store_true", help="print what would change, write nothing")
    args = parser.parse_args()
    try:
        if args.member:
            rename_members(args.names, args.filter, args.dry_run)
        elif args.unit:
            if len(args.names) != 2:
                parser.error("--unit OLD NEW")
            report(renaming.rename_unit(*args.names), args.dry_run)
        else:
            if len(args.names) % 2:
                parser.error("OLD NEW pairs")
            mapping = dict(zip(args.names[::2], args.names[1::2]))
            if len(mapping) != len(args.names) // 2:
                parser.error("a name is renamed twice")
            report(
                renaming.rename(mapping, code=not args.no_code, force=set(args.force), source=args.source), args.dry_run
            )
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
