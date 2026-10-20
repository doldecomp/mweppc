#!/usr/bin/env python3

import argparse
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

argparse.ArgumentParser(
    description="Build and compare every version (ninja verify), then check for regressions against the baseline."
).parse_args()
built = subprocess.run(["ninja", "verify"], cwd=ROOT).returncode
if not built and not (ROOT / "build/regressions").exists():
    print("no regressions baseline (build/regressions): saving this build's", flush=True)
    sys.exit(subprocess.run([sys.executable, str(ROOT / "tools/regressions.py"), "save"], cwd=ROOT).returncode)
checked = subprocess.run([sys.executable, str(ROOT / "tools/regressions.py")], cwd=ROOT).returncode
sys.exit(built or checked)
