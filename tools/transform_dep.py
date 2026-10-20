#!/usr/bin/env python3
"""python tools/transform_dep.py DEP OUT: MWCC's -MD dependency file (written to the working directory, its paths
Windows paths as wibo maps them) as a ninja depfile for OUT."""

import os
import re
import sys
from pathlib import Path

if sys.argv[1:2] in (["-h"], ["--help"]):
    print(__doc__)
    sys.exit()
dep, out = Path(sys.argv[1]), sys.argv[2]
paths = []
for token in re.split(r"\s+", dep.read_text(encoding="latin-1").replace("\\\n", " "))[1:]:
    if token:
        path = token.replace("\\", "/")
        if re.match(r"[A-Za-z]:/", path):
            path = path[2:]
        paths.append(os.path.relpath(path) if path.startswith("/") else path)
Path(out + ".d").write_text(out + ": " + " ".join(p.replace(" ", "\\ ") for p in paths) + "\n")
dep.unlink()
