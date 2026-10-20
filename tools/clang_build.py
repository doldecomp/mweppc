#!/usr/bin/env python3
"""The compiler built with clang (tools/mw/clang.py says how): ninja runs these steps.

python tools/clang_build.py link VERSION
    link build/VERSION/clang/mwcceppc.exe from the clang build's objects with lld-link: the objects of every source,
    src/imports' import libraries and build/VERSION/resources.res. A name nothing defines is a trap, which only code
    the link drops may refer to.
python tools/clang_build.py case STAMP
    make the lowercase links to lib/'s headers that a case-sensitive file system needs (build/clang/case: lib/'s
    Win32 SDK headers are WINNT.H and the like, included as winnt.h), then touch STAMP.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, project


def main():
    args = sys.argv[1:]
    if args[:1] in (["-h"], ["--help"]):
        print(__doc__)
        return
    if len(args) != 2 or args[0] not in ("link", "case"):
        sys.exit(__doc__.strip())
    try:
        if args[0] == "link":
            output = clang.link_compiler(project.version(args[1]))
            print(f"{args[1]}: {project.relative(output)}")
        else:
            clang.make_case_shims([c for v in project.ALL_VERSIONS for c in build.compilations(v)])
            stamp = Path(args[1])
            stamp.parent.mkdir(parents=True, exist_ok=True)
            stamp.touch()
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
