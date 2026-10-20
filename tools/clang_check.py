#!/usr/bin/env python3
"""Compile every source (whose path contains FILTER) with clang -fsyntax-only (tools/mw/clang.py: the primary
version's command), in parallel, and print its errors. The ones MWCC lets through matter: a call to an undeclared
function is an implicit int declaration to MWCC too, and changes the code around it.

Without `save`, only the errors the baseline (build/clang_errors.txt) lacks are printed (line numbers ignored), so a
change can be checked for what it newly breaks; it exits 1 when there are such (every error, when there is no
baseline). `save` keeps every source's errors as the baseline.
"""

import argparse
import re
import sys
from collections import Counter
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, isolated, project

BASELINE = project.ROOT / "build/clang_errors.txt"


def main():
    parser = argparse.ArgumentParser(
        prog="tools/clang_check.py",
        usage="%(prog)s [save | FILTER]",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("filter", nargs="?", default="", metavar="save | FILTER")
    args = parser.parse_args()
    save = args.filter == "save"
    try:
        compilations = [c for c in build.compilations() if save or args.filter in c.source]
        found = [line for lines in isolated.parallel(clang.errors, compilations) for line in lines]
    except project.Error as e:
        sys.exit(f"error: {e}")
    if save:
        project.write_atomic(BASELINE, "\n".join(found) + "\n")
        print(f"{len(found)} errors saved to {project.relative(BASELINE)}")
        return
    if not BASELINE.exists():
        print("\n".join(found))
        print(f"{len(found)} errors (no baseline: python tools/clang_check.py save)")
        sys.exit(1 if found else 0)

    def strip(line):
        return re.sub(r":\d+:\d+:", ":", line)

    base = Counter(map(strip, BASELINE.read_text().splitlines()))
    new = []
    for line in found:
        if base[strip(line)]:
            base[strip(line)] -= 1
        else:
            new.append(line)
    if new:
        print("\n".join(new))
    print(f"{len(found)} errors, {len(new)} not in the baseline")
    sys.exit(1 if new else 0)


if __name__ == "__main__":
    main()
