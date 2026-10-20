#!/usr/bin/env python3
"""python tools/regressions.py [save] [VERSION...]: what got worse since a saved baseline (default: every version).

save   keep each version's comparison (build/VERSION/compare.json) and state (config/VERSION/state.json) as the
       baseline, in build/regressions/VERSION/
check  (the default) the functions that were exact and are not any more, by address (a rename neither hides nor fakes
       one), with why; and the sources that linked into the original and do not now. Exits 1 when anything was lost.

Both refuse a comparison that is not current (run ninja, or ninja verify for every version, first).
"""

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import config, project, verdict

BASELINE = project.ROOT / "build/regressions"


def save(version):
    verdict.load(version)  # (refuses a stale comparison)
    out = BASELINE / version.name
    project.write_atomic(out / "compare.json", verdict.result_path(version).read_bytes())
    project.write_atomic(out / "state.json", (version.config / "state.json").read_bytes())
    print(f"{version}: saved")


def check(version) -> bool:
    base = BASELINE / version.name
    if not (base / "compare.json").exists():
        raise project.Error(f"no baseline for {version}: python tools/regressions.py save")
    now = verdict.load(version)
    before = json.loads((base / "compare.json").read_text())["functions"]
    lost = [
        (int(a, 16), f) for a, f in before.items() if f["exact"] and not (int(a, 16) in now and now[int(a, 16)].exact)
    ]
    old_state = config.state(version, (base / "state.json").read_text())
    linked_before = set(old_state["matching"]) - set(old_state["relink_exclude"])
    cfg = config.version_config(version)
    unlinked = sorted(s for s in linked_before if not cfg.linked(s))
    gained = sum(1 for a, v in now.items() if v.exact and not before.get(f"0x{a:08x}", {}).get("exact"))
    print(f"{version}: lost {len(lost)} exact functions, {len(unlinked)} linked sources; gained {gained} exact")
    for address, f in lost:
        why = now[address].reason if address in now else "no longer a row with a source"
        print(f"  0x{address:08x} {f['name']} ({f['source']}): {why}")
    for source in unlinked:
        print(f"  {source}: " + ("not Matching" if source not in cfg.matching else cfg.relink_exclude[source]))
    return not lost and not unlinked


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    args = sys.argv[1:]
    command = args.pop(0) if args and args[0] in ("save", "check") else "check"
    try:
        versions = [project.version(v, original=True) for v in args] or list(project.VERSIONS)
        if command == "save":
            for v in versions:
                save(v)
        else:
            ok = [check(v) for v in versions]
            sys.exit(0 if all(ok) else 1)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
