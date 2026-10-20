#!/usr/bin/env python3

import argparse
import os
import re
from pathlib import Path

parser = argparse.ArgumentParser(description="MWCC's -MD dependency file as a ninja depfile for OUT.")
parser.add_argument("dep", type=Path, metavar="DEP")
parser.add_argument("outs", nargs="+", metavar="OUT", help="the object (ninja may name further outputs after it)")
args = parser.parse_args()
dep, out = args.dep, args.outs[0]
paths = []
for token in re.split(r"\s+", dep.read_text(encoding="latin-1").replace("\\\n", " "))[1:]:
    if token:
        path = token.replace("\\", "/")
        if re.match(r"[A-Za-z]:/", path):
            path = path[2:]
        paths.append(os.path.relpath(path) if path.startswith("/") else path)
Path(out + ".d").write_text(out + ": " + " ".join(p.replace(" ", "\\ ") for p in paths) + "\n")
dep.unlink()
