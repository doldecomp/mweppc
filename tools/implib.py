#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import link, project


def main():
    parser = argparse.ArgumentParser(
        description="An import library (OUTDIR/<dll>.lib) for each DLL the original executable imports from."
    )
    parser.add_argument("version", metavar="VERSION")
    parser.add_argument("outdir", metavar="OUTDIR")
    args = parser.parse_args()
    version, outdir = args.version, args.outdir
    try:
        for name, data in link.import_libraries_of(project.version(version, original=True)).items():
            project.write_atomic(Path(outdir, name), data)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
