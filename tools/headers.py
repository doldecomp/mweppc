#!/usr/bin/env python3
"""python tools/headers.py COMMAND ...: headers as the units' APIs.

own HEADER... [--version V] [--dry-run]
    make each HEADER the one place its functions and extern variables are declared: their `extern` declarations
    elsewhere (in src/ and include/, as clang reads every source) are removed, and printed. A unit's prototypes
    without `extern` (forward declarations of its own functions) stay: declarations in scope can change MWCC's code.
    One inside an #if, or declared in a statement with other names (`extern int a, b;`), is listed and left. Build
    after: a file that relied on a removed copy now needs the header (tools/includes.py add or fix).
bisect SOURCE [--rev REV] [--version V]
    which changed files change SOURCE's compiled code: SOURCE (as it is) is compiled against REV's include/ and src/
    (default HEAD), then once per file changed since (in the working tree, untracked ones included) with that one
    file as it is now; each whose swap changes the object (every section but .bss and the debug ones, relocations
    by name) is printed. For a function that stops matching after a header edit that should not concern it: MWCC's
    code can follow the declarations in scope. Each compile is the build's own command line, run in a copy of the
    tree, so even the include paths' lengths are the build's.
"""

import argparse
import os
import shutil
import subprocess
import sys
import tarfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, csrc, isolated, project
from mw.coff import Object

ROOT = project.ROOT


# ------------------------------------------------------------------------------------------------------------- own


def owned(d: csrc.Declaration) -> bool:
    """Whether D is a declaration a header owns: a function's (not static, no body) or an extern variable's."""
    if d.defined or d.storage == "static":
        return False
    return d.kind == "FunctionDecl" or (d.kind == "VarDecl" and d.storage == "extern")


def own(args):
    v = project.version(args.version)
    headers = [project.relative(Path(h).resolve()) for h in args.headers]
    for h in headers:
        if not (ROOT / h).is_file():
            raise project.Error(f"{h} does not exist")
    everything = csrc.project_declarations(v)
    seen_files = {d.file for d in everything}
    removals, left = {}, []
    for header in headers:
        if header not in seen_files:
            raise project.Error(f"{header}: no source includes it (clang never reads it)")
        names = {d.name for d in everything if d.file == header and owned(d)}
        for d in everything:
            if (
                d.name not in names
                or d.file == header
                or not owned(d)
                or d.storage != "extern"
                or not d.file.startswith(("src/", "include/"))
            ):
                continue
            if d.file in headers:
                left.append(f"{d.file}: {d.name}: also in {header}, which owns its own (left)")
                continue
            if d.file not in removals:
                removals[d.file] = (csrc.Text(d.file), [])
            text = removals[d.file][0]
            end = text.statement_end(d.end)
            line = text.line(d.start)
            if d.shared:
                left.append(f"{d.file}:{line}: {d.name}: declared with other names in one statement (left)")
            elif text.conditional_at(d.start):
                left.append(f"{d.file}:{line}: {d.name}: inside an #if (left)")
            elif end is None:
                left.append(f"{d.file}:{line}: {d.name}: no `;` after it (left)")
            else:
                start = text.line_start(d.start) if text.only_space(text.line_start(d.start), d.start) else d.start
                removals[d.file][1].append((start, end))
    for note in left:
        print(note)
    with project.Transaction() as tx:
        for path, (text, spans) in sorted(removals.items()):
            if not spans:
                continue
            tx.unchanged(path, text.text)
            out = text.text
            for start, end in sorted(set(spans), reverse=True):
                print(f"{path}: {' '.join(out[start:end].split())}")
                out = out[:start] + out[end:]
            if not args.dry_run:
                tx.write(path, out.encode("latin-1"))
    if args.dry_run:
        print("(nothing written)")


# ---------------------------------------------------------------------------------------------------------- bisect


def git(*args) -> str:
    try:
        result = subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True)
    except FileNotFoundError:
        raise project.Error("bisect needs git") from None
    if result.returncode:
        raise project.Error(f"git {' '.join(args)}: {result.stderr.strip()}")
    return result.stdout


def bisect(args):
    v = project.version(args.version)
    source = project.relative(Path(args.source).resolve())
    compilation = build.compilation(v, source)
    git("rev-parse", "--verify", f"{args.rev}^{{commit}}")  # (a checkout, and REV one of its commits)
    changed = git("diff", "--name-only", args.rev, "--", "include", "src").split()
    changed += git("ls-files", "--others", "--exclude-standard", "--", "include", "src").split()
    changed = sorted(set(changed) - {source})
    if not changed:
        print(f"nothing in include/ or src/ changed since {args.rev}")
        return
    with isolated.Workspace() as workspace:
        archive = workspace.path / "rev.tar"
        archive.write_bytes(
            subprocess.run(
                ["git", "archive", args.rev, "include", "src"], cwd=ROOT, capture_output=True, check=True
            ).stdout
        )
        base = workspace.path / "base"
        with tarfile.open(archive) as tar:
            tar.extractall(base, filter="data")
        archive.unlink()
        (base / source).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(ROOT / source, base / source)

        def tree(name: str, swapped: str | None) -> Path:
            """A copy of the base (hard links) with SWAPPED as it is now; lib/ and build/ (the compilers) linked."""
            path = workspace.path / f"trial-{name}"
            shutil.copytree(base, path, copy_function=os.link)
            for shared in ("lib", "build"):
                (path / shared).symlink_to(ROOT / shared)
            if swapped:
                (path / swapped).unlink(missing_ok=True)
                if (ROOT / swapped).exists():
                    (path / swapped).parent.mkdir(parents=True, exist_ok=True)
                    shutil.copyfile(ROOT / swapped, path / swapped)
            return path

        def compile_in(job):
            name, swapped = job
            root = tree(name, swapped)
            result = build.run(compilation.command("out.obj"), cwd=root)
            obj = root / "out.obj"
            return (
                isolated.comparable(Object.read(obj)) if result.returncode == 0 and obj.exists() else None
            ), result.stdout + result.stderr

        jobs = [("base", None)] + [(str(i), path) for i, path in enumerate(changed)]
        results = list(isolated.parallel(compile_in, jobs))
    (reference, log), rest = results[0], results[1:]
    if reference is None:
        raise project.Error(f"{source} does not compile against {args.rev}'s include/ and src/:\n{log[-2000:]}")
    found = 0
    for path, (obj, _) in zip(changed, rest):
        if obj is None:
            print(f"does not compile with only this one changed: {path}")
        elif obj != reference:
            print(f"changes the code: {path}")
            found += 1
    print(f"{len(changed)} changed files tried; {found} change {source}'s code")


def main():
    parser = argparse.ArgumentParser(
        prog="tools/headers.py",
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__[__doc__.index("\n\n") + 2 :],
    )
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--version", help=f"default {project.PRIMARY}")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    p = commands.add_parser("own", parents=[common], help="make each HEADER the one place of its declarations")
    p.add_argument("headers", nargs="+", metavar="HEADER")
    p.add_argument("--dry-run", action="store_true", help="print the removals, write nothing")
    p = commands.add_parser("bisect", parents=[common], help="which changed files change SOURCE's code")
    p.add_argument("source", metavar="SOURCE")
    p.add_argument("--rev", default="HEAD", help="the revision to start from (default HEAD)")
    args = parser.parse_args()
    try:
        own(args) if args.command == "own" else bisect(args)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
