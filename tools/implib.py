#!/usr/bin/env python3
"""python tools/implib.py VERSION OUTDIR: an import library (OUTDIR/<dll>.lib) for each DLL the original executable of
VERSION imports from, with the names, hints and ordinals the original's import directory has (tools/mw/link.py's
import_library). Each import is data (an __imp_ pointer and no thunk): the thunks the original has are its split
objects'. The __imp_ symbols are named as config/VERSION/symbols.txt names them (their decorations)."""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import link, project


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip())
    version, outdir = sys.argv[1:]
    try:
        for name, data in link.import_libraries_of(project.version(version, original=True)).items():
            project.write_atomic(Path(outdir, name), data)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
