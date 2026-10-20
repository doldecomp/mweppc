#!/usr/bin/env python3
"""python tools/includes.py (trim | add | fix | sort) SOURCE... [--dry-run]: keep sources' #include lists to what they use.

  trim  drop each "compiler/" or "driver/" include (not the source's own header, not common.h) whose removal changes
        nothing: no new clang error and the same MWCC object (every section but .bss and debug) in every version.
        MWCC's code can follow the declarations in scope, so clang alone does not decide. (clangd's include-cleaner is
        no guide here: it credits a struct to the typedef in common.h and calls the header defining it unused.)
  add   for each clang error naming an undeclared function, identifier or type, include the header that declares
        it (when one header of include/compiler or include/driver does; several are listed, not chosen).
  fix   compile with MWCC and, for each "undefined identifier X" or "incomplete struct X" error, include the header
        that defines X (declarations as clang reads them; macros from the headers' #defines); repeat until it
        compiles or nothing is left to add.
  sort  sort each run of includes before the source's first code: its own header, the Iro* headers, the other
        compiler/ headers, driver/, then <system> ones; common.h goes where the own header is there (every header
        includes it). Includes after code or inside an #if, and the lines between runs (a #define the next includes
        read), stay where they are.

Only an include before the first code and outside any #if is removed, and added ones go after the last of those.
Sources run in parallel; each file's edit is atomic and fails if the file changed meanwhile. MWCC's code generation
can change with what is in scope: check with tools/verify.py after.
"""

import argparse
import collections
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, config, csrc, isolated, project
from mw.coff import Object

ROOT = project.ROOT
PROVIDERS = ("include/compiler/", "include/driver/")


def source_path(name: str) -> str:
    path = project.relative(Path(name).resolve())
    if path not in config.sources():
        raise project.Error(f"{path} is not in config/sources.json")
    return path


def write(path: str, before: str, after: str, dry_run: bool) -> None:
    if dry_run or after == before:
        return
    with project.Transaction() as tx:
        tx.unchanged(path, before)
        tx.write(path, after.encode("latin-1"))


def remove_line(text: str, directive: csrc.Directive) -> str:
    return text[: directive.start] + text[directive.end + 1 :]


def leading(text: csrc.Text) -> list[csrc.Include]:
    """TEXT's includes before its first code and outside any #if."""
    return [i for i in text.includes() if not i.conditional and not i.late]


# ------------------------------------------------------------------------------------------------------------ trim


def _errors(path: str, text: str) -> list[collections.Counter]:
    """clang's errors under each version, positions dropped (removing a line moves the later ones)."""
    return [
        collections.Counter(re.sub(r":\d+:\d+:", ":", e) for e in clang.errors(build.compilation(v, path), text))
        for v in project.VERSIONS
    ]


def _objects(path: str, text: str, workspace: isolated.Workspace) -> list:
    out = []
    for v in project.VERSIONS:
        result = workspace.compile(build.compilation(v, path), text)
        out.append(isolated.comparable(Object.read(result.object)) if result.ok else None)
        if result.object:
            result.object.unlink()
    return out


def trim(path: str, workspace: isolated.Workspace, dry_run: bool) -> str:
    source = csrc.Text(path)
    own = Path(path).stem.lower()
    candidates = [
        i
        for i in leading(source)
        if re.fullmatch(r'"(compiler|driver)/\w+\.h"', i.target)
        and Path(i.target.strip('"')).stem.lower() not in (own, "common")
    ]
    if not candidates:
        return f"{path}: nothing to try"

    def outcome(text):
        return _errors(path, text), _objects(path, text, workspace)

    base = outcome(source.text)
    if None in base[1]:
        return f"{path}: does not compile with MWCC as it is (in some version): not trimmed"

    def without(includes):
        text = source.text
        for i in sorted(includes, key=lambda i: i.directive.start, reverse=True):
            text = remove_line(text, i.directive)
        return text

    # (each alone, in parallel; then all that pass together, or one at a time when together they do not)
    alone = list(isolated.parallel(lambda i: outcome(without([i])) == base, candidates))
    removable = [i for i, ok in zip(candidates, alone) if ok]
    if removable and outcome(without(removable)) != base:
        kept = []
        for i in removable:
            if outcome(without(kept + [i])) == base:
                kept.append(i)
        removable = kept
    write(path, source.text, without(removable), dry_run)
    return f"{path}: -{len(removable)} " + " ".join(i.target.strip('"') for i in removable)


# ------------------------------------------------------------------------------------------------------- add, fix


def providers(version, macros: bool) -> dict[str, set[str]]:
    """{name: the headers of include/compiler and include/driver declaring it} (with MACROS, their #defines too)."""
    out = collections.defaultdict(set)
    for d in csrc.project_declarations(version):
        if d.file.startswith(PROVIDERS) and d.storage != "static" and d.name:
            if d.kind in ("FunctionDecl", "VarDecl", "TypedefDecl", "RecordDecl", "EnumDecl"):
                out[d.name].add(d.file)
    if macros:
        for directory in PROVIDERS:
            for header in sorted((ROOT / directory).glob("*.h")):
                text = csrc.Text(project.relative(header))
                for x in text.directives:
                    m = re.match(r"#\s*define\s+(\w+)", x.text) if x.kind == "define" else None
                    if m:
                        out[m[1]].add(text.path)
    return out


def include_header(text: str, header: str) -> str | None:
    """TEXT with `#include "HEADER"` (a path under include/) after its last leading include; None when it has it."""
    trivia = csrc.Text("", text)
    name = header.removeprefix("include/")
    if any(i.target == f'"{name}"' for i in trivia.includes()):
        return None
    lines = leading(trivia)
    at = lines[-1].directive.end + 1 if lines else trivia.line_start(trivia.first_code)
    return text[:at] + f'#include "{name}"\n' + text[at:]


def choose(path: str, name: str, found: dict[str, set[str]], notes: list[str]) -> str | None:
    headers = sorted(found.get(name, ()))
    if len(headers) > 1:
        notes.append(f"{path}: {name} is declared in {', '.join(headers)}: include one by hand")
        return None
    return headers[0] if headers else None


def add(path: str, found, dry_run: bool) -> str:
    v = project.version(None)
    text = before = csrc.Text(path).text
    notes, added = [], []
    for line in clang.errors(build.compilation(v, path), text):
        m = re.search(r"(?:call to undeclared function|use of undeclared identifier|unknown type name) '(\w+)'", line)
        header = choose(path, m[1], found, notes) if m else None
        if header:
            new = include_header(text, header)
            if new is not None:
                text = new
                added.append(header.removeprefix("include/"))
    write(path, before, text, dry_run)
    return "\n".join(notes + [f"{path}: +{len(added)} {' '.join(added)}"])


def fix(path: str, found, workspace: isolated.Workspace, dry_run: bool) -> str:
    v = project.version(None)
    text = before = csrc.Text(path).text
    notes, added = [], []
    for _ in range(10):
        result = workspace.compile(build.compilation(v, path), text)
        if result.ok:
            break
        progress = False
        for m in re.finditer(
            r"incomplete struct/union/class '(?:struct|union|class) (\w+)'|"
            r"undefined identifier '(\w+)'",
            result.log,
        ):
            header = choose(path, m[1] or m[2], found, notes)
            new = include_header(text, header) if header else None
            if new is not None:
                text, progress = new, True
                added.append(header.removeprefix("include/"))
        if not progress:
            notes.append(f"{path}: does not compile, and nothing more to include:\n{result.log[-1500:]}")
            break
    write(path, before, text, dry_run)
    return "\n".join(notes + [f"{path}: +{len(added)} {' '.join(added)}"])


# ------------------------------------------------------------------------------------------------------------ sort


def sort_key(target: str, own: str):
    name = target.strip('"<>')
    stem = Path(name).stem
    if target.startswith('"') and stem.lower() == own:
        return 0, ""
    if target.startswith("<"):
        return 4, stem.lower()
    if stem.startswith(("Iro", "IRO", "IrOpt")):
        return 1, stem.lower()
    return (2 if name.startswith("compiler/") else 3), stem.lower()


def sort(path: str, dry_run: bool) -> str:
    source = csrc.Text(path)
    text = source.text
    own = Path(path).stem.lower()
    has_own = any(sort_key(i.target, own)[0] == 0 for i in source.includes())
    # (runs: includes with nothing but space between them)
    runs, run = [], []
    for i in leading(source):
        if run and source.only_space(run[-1].directive.end, i.directive.start):
            run.append(i)
        else:
            if run:
                runs.append(run)
            run = [i]
    if run:
        runs.append(run)
    for run in reversed(runs):
        lines = [(i.target, text[i.directive.start : i.directive.end]) for i in run]
        if has_own:
            lines = [x for x in lines if x[0] != '"compiler/common.h"']
        lines.sort(key=lambda x: sort_key(x[0], own))
        start, end = run[0].directive.start, run[-1].directive.end
        text = text[:start] + "\n".join(line for _, line in lines) + text[end:]
    write(path, source.text, text, dry_run)
    return f"{path}: {'sorted' if text != source.text else 'in order'}"


def main():
    import signal

    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # (piped into head: stop quietly)
    parser = argparse.ArgumentParser(
        prog="tools/includes.py",
        description=__doc__.split("\n\n")[0],
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__[__doc__.index("\n\n") + 2 :],
    )
    parser.add_argument("action", choices=("trim", "add", "fix", "sort"))
    parser.add_argument("sources", nargs="+", metavar="SOURCE")
    parser.add_argument("--dry-run", action="store_true", help="print what would change")
    args = parser.parse_args()
    try:
        paths = [source_path(s) for s in args.sources]
        with isolated.Workspace() as workspace:
            if args.action == "trim":
                results = isolated.parallel(lambda p: trim(p, workspace, args.dry_run), paths, jobs=4)
            elif args.action == "sort":
                results = map(lambda p: sort(p, args.dry_run), paths)
            else:
                found = providers(None, macros=args.action == "fix")
                if args.action == "add":
                    results = map(lambda p: add(p, found, args.dry_run), paths)
                else:
                    results = map(lambda p: fix(p, found, workspace, args.dry_run), paths)
            for line in results:
                print(line, flush=True)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
