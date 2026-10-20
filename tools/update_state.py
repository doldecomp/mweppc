#!/usr/bin/env python3
"""python tools/update_state.py [VERSION...]: bring each version's generated state up to date with its last comparison
(build/VERSION/compare.json; default: every version).

- config/VERSION/state.json "matching": the sources every function of which is exact (a source with no functions, a
  data-only unit, counts: tools/relink.py then decides whether its object links). The comparison fails when a
  function of a Matching source stops being exact.

A source that is no longer Matching loses its relink exclusion; run tools/relink.py for new Matching sources.
"""

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import config, project, verdict


def update(version, tx):
    results = verdict.load(version)
    rows = verdict.rows(version)
    exact = {a for a, v in results.items() if v.exact}
    inexact_sources = {r.source for r in rows if r.source and r.address not in exact}
    matching = sorted(s for s in config.sources() if s not in inexact_sources)
    path = f"config/{version}/state.json"
    old = config.state(version, tx.read(path))
    tx.write(path, config.dump_state(matching, {s: why for s, why in old["relink_exclude"].items() if s in matching}))
    gained, lost = sorted(set(matching) - set(old["matching"])), sorted(set(old["matching"]) - set(matching))
    print(
        f"{version}: {len(matching)} Matching sources"
        + (f"; now Matching: {', '.join(gained)}" if gained else "")
        + (f"; no longer Matching: {', '.join(lost)}" if lost else "")
    )


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    try:
        versions = [project.version(v, original=True) for v in sys.argv[1:]] or list(project.VERSIONS)
        with project.Transaction() as tx:
            for v in versions:
                update(v, tx)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
