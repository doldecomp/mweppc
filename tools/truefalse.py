#!/usr/bin/env python3
"""The integer literals 0 and 1 the sources use as Boolean values, written as FALSE and TRUE (the sources' style:
yes/no values are TRUE and FALSE, common.h's constants, which compile identically to the literals).

Every source (whose path contains FILTER) is read by clang (tools/mw/clang.py: the primary version's command), in
parallel; a literal 0 or 1 that clang converts to `Boolean` (an assignment, an argument, a return, an initializer) is
listed, in the source or a project header it includes. Literals written inside a macro's expansion, and code clang
does not see (another version's #if branch), are left alone. --write rewrites them (one transaction, laid out by
.clang-format); rebuild and check every version after (tools/verify.py).
"""

import argparse
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, isolated, project


def literals(compilation) -> list[tuple[str, int, str]]:
    """(file, offset, "0" or "1") of each literal clang converts to Boolean in COMPILATION's translation unit."""
    tree = clang.syntax_tree(compilation)
    found = []
    current = [None]

    def note(loc):
        # clang's JSON names a location's file only when it changes, in traversal order
        if isinstance(loc, dict) and "file" in loc:
            current[0] = loc["file"]

    def visit(node, parent):
        note(node.get("loc"))
        if "range" in node:
            note(node["range"].get("begin"))
            note(node["range"].get("end"))
        if (
            node.get("kind") == "IntegerLiteral"
            and node.get("value") in ("0", "1")
            and parent is not None
            and parent.get("kind") == "ImplicitCastExpr"
            and parent.get("type", {}).get("qualType") == "Boolean"
        ):
            begin = node["range"]["begin"]
            if "offset" in begin and "spellingLoc" not in begin and "expansionLoc" not in begin and current[0]:
                found.append((current[0], begin["offset"], node["value"]))
        for child in node.get("inner", ()):
            visit(child, node)

    sys.setrecursionlimit(100000)
    visit(tree, None)
    return found


def main():
    parser = argparse.ArgumentParser(
        prog="tools/truefalse.py",
        usage="%(prog)s [--write] [FILTER]",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("filter", nargs="?", default="", metavar="FILTER", help="only sources whose path contains it")
    parser.add_argument("--write", action="store_true", help="rewrite the literals")
    args = parser.parse_args()
    try:
        compilations = [c for c in build.compilations() if args.filter in c.source]
        edits = collections.defaultdict(set)
        for found in isolated.parallel(literals, compilations):
            for file, offset, value in found:
                path = Path(file) if Path(file).is_absolute() else project.ROOT / file
                if project.ROOT in path.parents and clang.formatted(path):
                    edits[path].add((offset, value))
        with project.Transaction() as tx:
            for path, found in sorted(edits.items()):
                text = tx.read_bytes(path).decode("latin-1")
                for offset, value in sorted(found, reverse=True):
                    if text[offset] != value or text[offset + 1 : offset + 2].isalnum():
                        raise project.Error(f"{project.relative(path)}: offset {offset} is not the literal {value}")
                    text = text[:offset] + ("TRUE" if value == "1" else "FALSE") + text[offset + 1 :]
                print(f"{project.relative(path)}: {len(found)}")
                if args.write:
                    tx.write(path, text.encode("latin-1"))
    except project.Error as e:
        sys.exit(f"error: {e}")
    total = sum(len(v) for v in edits.values())
    print(f"{total} literals in {len(edits)} files" + ("" if args.write else " (--write rewrites them)"))


if __name__ == "__main__":
    main()
