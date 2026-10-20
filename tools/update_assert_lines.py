#!/usr/bin/env python3
"""python tools/update_assert_lines.py [VERSION] [--write]: the line numbers the sources' asserts pass, where the
original's differ (after a build: it compares the build's objects).

An assert compiles to pushes of its line and file name and a call of the assert function (tools/mw/asserts.py). In
each function of the sources, the assert calls are lined up with the original's (by instruction); each pair whose
lines differ says the source's line should be the original's. Exact functions take part too, each site agreeing with
itself, so a line number one function needs changed and another has right is not changed for both.

A (file, line) every site agrees to change is changed wherever an assert with that file name passes the line. One the
sites disagree about is changed only inside the functions that need it (their bodies in their own sources: an assert
inlined from a header is listed, not changed). --write makes the changes: CError_ASSERT, CError_FATAL, CError_FAIL and
the other assert macros (line first, under the file name CERROR_FILE gives at that point), and calls of
CError_Internal and __assertion_failed with the file named. Only the primary version writes: the sources are its.

Another version's lines are the primary's plus CError_LINE_SHIFT (include/compiler/CError.h), which a file redefines
for that version where its source file had other lines above (`#if VERSION ...`, `#undef`, `#define`). For such a
version the listing ends with the shifts: in each file, from which line (the one its assert passes now) on what the
shift should change by, and in which function that site is.
"""

import argparse
import collections
import difflib
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import asserts, build, cindex, project, verdict
from mw.coff import DIR32, CoffError, Object, function_extent
from mw.names import is_mangled
from mw.pe import original
from mw.renaming import code_files

_MACRO = re.compile(r"\b(\w*(?:ASSERT|FATAL|FAIL)\w*\s*\(\s*)(0x[0-9a-fA-F]+|\d+)(?=\s*[,)])")
_EXPLICIT = re.compile(
    r'((?:CError_Internal|__assertion_failed)\s*\((?:[^;]*?,\s*)?"([\w.+-]+)",\s*)'
    r"(0x[0-9a-fA-F]+U?|\d+U?)"
)


def literal_strings(version, row):
    """{address of a field of ROW's compiled function: the string it refers to}, from the source's object."""
    try:
        obj = Object.read(build.object_path(version, row.source))
    except (FileNotFoundError, CoffError):
        return {}
    linker = row.symbol if is_mangled(row.symbol) else "_" + row.symbol
    found = [s for s in obj.symbols if s.defined and s.name == linker]
    if len(found) != 1:
        return {}
    section, start, end = function_extent(obj, found[0])
    out = {}
    for r in section.relocations:
        if start <= r.offset < end and r.kind == DIR32:
            target = obj.symbol(r.symbol)
            if not target.defined or obj.section(target.section).code or obj.section(target.section).bss:
                continue
            data = obj.section(target.section).data
            at = target.value + struct.unpack_from("<I", section.data, r.offset)[0]
            out[row.address + r.offset - start] = data[at : at + 64].split(b"\0", 1)[0].decode("latin-1")
    return out


def pairs_of(version):
    """[(row, file, source line, original line)] of every assert site the comparison lines up."""
    pe = original(version)
    rows = verdict.rows(version)
    targets = asserts.targets(rows)
    results = verdict.compare(version)
    out = []
    for row in rows:
        v = results.get(row.address)
        if v is None or v.body is None:
            continue
        theirs, their_sites = asserts.sites(pe, pe.read(row.address, row.size), row.address, targets)
        if v.exact:
            out += [(row, s.file, s.line, s.line) for s in their_sites]
            continue
        mine, my_sites = asserts.sites(pe, v.body, row.address, targets, literal_strings(version, row))
        if not my_sites or not their_sites:
            continue
        matcher = difflib.SequenceMatcher(
            None, [i.mnemonic for i in mine], [i.mnemonic for i in theirs], autojunk=False
        )
        aligned = {block.a + k: block.b + k for block in matcher.get_matching_blocks() for k in range(block.size)}
        by_index = {s.index: s for s in their_sites}
        for s in my_sites:
            other = by_index.get(aligned.get(s.index))
            if other and other.file == s.file:
                out.append((row, s.file, s.line, other.line))
    return out


_applied = set()  # (id(mapping), line) of each change made


def replace(literal, mapping):
    suffix = "U" if literal.endswith("U") else ""
    value = int(literal.rstrip("U"), 0)
    if value not in mapping:
        return literal
    _applied.add((id(mapping), value))
    return (f"0x{mapping[value]:x}" if literal.lower().startswith("0x") else str(mapping[value])) + suffix


def rewrite(text, name, by_file, lines=None):
    """TEXT (of a file named NAME) with the assert lines BY_FILE {file: {line: new}} changes, only within LINES
    (a range of 1-based line numbers) when given."""
    out, current = [], name
    for number, line in enumerate(text.split("\n"), 1):
        defined = re.match(r'\s*#\s*define\s+CERROR_FILE\s+"([^"]+)"', line)
        if defined:
            current = defined[1]
        elif re.match(r"\s*#\s*undef\s+CERROR_FILE", line):
            current = name
        elif not line.lstrip().startswith("#") and (lines is None or number in lines):
            mapping = by_file.get(current, {})
            if mapping:
                line = _MACRO.sub(lambda m: m[1] + replace(m[2], mapping), line)
            if lines is not None:
                line = _EXPLICIT.sub(lambda m: explicit(m, by_file), line)
        out.append(line)
    text = "\n".join(out)
    # (explicit file names, a call possibly over several lines: CError_Internal("x.c", n), __assertion_failed(cond,
    # "x.c", n))
    return _EXPLICIT.sub(lambda m: explicit(m, by_file), text) if lines is None else text


def explicit(m, by_file):
    return m[1] + replace(m[3], by_file[m[2]]) if m[2] in by_file else m[0]


def print_shifts(found):
    """The runs of FOUND's sites (in each file, by the line they pass) that need the same change."""
    by_file = collections.defaultdict(dict)
    for row, file, line, new in found:
        by_file[file][line] = (new - line, row.name)
    for file, sites in sorted(by_file.items()):
        if not any(delta for delta, _ in sites.values()):
            continue
        print(f"  {file}:")
        previous = 0
        for line in sorted(sites):
            delta, name = sites[line]
            if delta != previous:
                print(f"    from {line} ({name}): {delta:+d}")
                previous = delta


def main():
    parser = argparse.ArgumentParser(
        prog="tools/update_assert_lines.py",
        usage="%(prog)s [VERSION] [--write]",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version", nargs="?", metavar="VERSION", help=f"default {project.PRIMARY}")
    parser.add_argument("--write", action="store_true", help=f"change the sources ({project.PRIMARY} only)")
    args = parser.parse_args()
    write = args.write
    try:
        version = project.version(args.version, original=True)
        if write and version.name != project.PRIMARY:
            raise project.Error(f"only {project.PRIMARY} writes: the sources' lines are its")
        found = pairs_of(version)
        votes = collections.defaultdict(collections.Counter)
        for _, file, line, new in found:
            votes[(file, line)][new] += 1
        everywhere = collections.defaultdict(dict)  # file -> {line: new}
        local = collections.defaultdict(lambda: collections.defaultdict(dict))  # row address -> file -> {line: new}
        row_at = {}
        for row, file, line, new in found:
            counter = votes[(file, line)]
            if line == new:
                continue
            if len(counter) == 1:
                everywhere[file][line] = new
            else:
                local[row.address][file][line] = new
                row_at[row.address] = row
        print(
            f"{version}: {sum(len(m) for m in everywhere.values())} assert lines to change everywhere, in "
            f"{len(everywhere)} files; {sum(len(m) for f in local.values() for m in f.values())} inside "
            f"{len(local)} functions only"
        )
        if not write:
            if version.name != project.PRIMARY:
                print_shifts(found)
                return
            for file, lines in sorted(everywhere.items()):
                print(f"  {file}: {len(lines)}")
            for address, files in sorted(local.items()):
                print(
                    f"  {row_at[address].name}: "
                    + ", ".join(f"{f}:{l}->{n}" for f, m in files.items() for l, n in m.items())
                )
            return
        tx = project.Transaction()
        for path in code_files():
            if path.suffix in (".c", ".cpp", ".h"):
                text = tx.read_bytes(path).decode("latin-1")
                new = rewrite(text, path.name, everywhere)
                for address, files in local.items():
                    row = row_at[address]
                    if row.source != project.relative(path):
                        continue
                    for f, m in files.items():
                        if f != path.name:
                            print(f"left: {row.name}: {f}:{','.join(map(str, m))} (inlined from another file)")
                    body = next((d for d in cindex.functions(path) if d["name"] == row.name), None)
                    if body is None:
                        print(f"left: {row.name}: no definition found in {row.source}")
                        continue
                    own = files.get(path.name, {})
                    new = rewrite(new, path.name, {path.name: own}, range(body["line"], body["line"] + body["lines"]))
                    for line, target in sorted(own.items()):
                        if (id(own), line) not in _applied:
                            print(f"left: {row.name}: {path.name}:{line} -> {target}: no assert in its body passes it")
                if new != text:
                    tx.write(path, new.encode("latin-1"))
        for file, mapping in everywhere.items():
            for line, new in sorted(mapping.items()):
                if (id(mapping), line) not in _applied:
                    print(
                        f"left: {file}:{line} -> {new}: no assert in the sources passes it (a file name in a "
                        f"variable, or a line through a helper)"
                    )
        print(f"rewrote {len(tx.commit())} files")
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
