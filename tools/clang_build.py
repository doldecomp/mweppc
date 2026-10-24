#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, project


def main():
    parser = argparse.ArgumentParser(description="The clang build's steps (ninja runs them).")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    command = commands.add_parser("link", help="link build/VERSION/clang/mwcceppc.exe from the clang build's objects")
    command.add_argument("version", metavar="VERSION")
    command = commands.add_parser("native", help="link build/VERSION/native/mwcceppc, the Linux build, with ld.lld")
    command.add_argument("version", metavar="VERSION")
    command = commands.add_parser(
        "case", help="make the lowercase links to lib/'s headers a case-sensitive file system needs, then touch STAMP"
    )
    command.add_argument("stamp", metavar="STAMP")
    args = parser.parse_args()
    try:
        if args.command in ("link", "native"):
            link = clang.link_compiler if args.command == "link" else clang.link_native
            output = link(project.version(args.version))
            print(f"{args.version}: {project.relative(output)}")
        else:
            clang.make_case_shims([c for v in project.ALL_VERSIONS for c in build.compilations(v)])
            stamp = Path(args.stamp)
            stamp.parent.mkdir(parents=True, exist_ok=True)
            stamp.touch()
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
