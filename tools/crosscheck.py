#!/usr/bin/env python3
"""python tools/crosscheck.py [VERSION]: where the comparison's verdict on a function (build/VERSION/compare.json,
tools/mw/verdict.py) and objdiff's (the --matching build's build/VERSION/report.json, `ninja progress`) disagree, and why.

The comparison calls a function exact when its compiled bytes, every reference resolved to the original's address,
are the original's, and every datum it reaches is (tools/mw/verdict.py). objdiff compares the compiled object with the
unit's split object, function by function, relocations by the names of their targets. They disagree when:
  - a function is in another unit than its source: the split (the original's layout) puts its address in another
    translation unit, so objdiff has no function of that name to compare it with;
  - a name is an alias: the linker kept one copy of identical functions, and the report lists one name per address,
    or symbols.txt names the address otherwise;
  - objdiff scores 100% what is not the original: a reference to the wrong variable or literal, which objdiff, going by
    names, does not see;
  - the comparison's function is exact and objdiff's is not: the split function runs on over code functions.json does not
    list (a function missing from it).
Run after a build of both (python configure.py --matching && ninja progress: it compares too)."""

import json
import re
import sys
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import formats, project, verdict


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    try:
        version = project.version(sys.argv[1] if len(sys.argv) > 1 else None)
        results = verdict.load(version)
        rows = verdict.rows(version)
    except project.Error as e:
        sys.exit(f"error: {e}")
    report_path = version.build / "report.json"
    if not report_path.exists():
        sys.exit(f"error: {project.relative(report_path)} does not exist: configure --matching, then ninja progress")
    if report_path.stat().st_mtime < (version.build / "compare.json").stat().st_mtime:
        sys.exit(
            f"error: {project.relative(report_path)} is older than the comparison: "
            f"ninja {project.relative(report_path)} (with the build configured --matching)"
        )
    report = json.loads(report_path.read_text())
    undecorate = lambda name: re.sub(r"@\d+$", "", name)
    scores = {}
    for unit in report["units"]:
        for function in unit.get("functions", []):
            scores[(unit["name"], undecorate(function["name"]))] = function.get("fuzzy_match_percent") or 0.0
    spans = sorted(
        (r.start, r.end, u.name)
        for u in formats.read_splits(version.config / "splits.txt").units
        for r in u.ranges
        if r.section == ".text"
    )
    starts = [start for start, _, _ in spans]

    def unit_at(address):
        i = bisect_right(starts, address) - 1
        return spans[i][2] if i >= 0 and spans[i][0] <= address < spans[i][1] else None

    names_at = defaultdict(set)
    for symbol in formats.read_symbols(version.config / "symbols.txt"):
        if symbol.section == ".text" and symbol.kind == "function":
            names_at[symbol.address].add(symbol.name)

    elsewhere, aliases, renamed, too_kind, cut_short, agree = [], [], [], [], [], 0
    for row in rows:
        if not row.source or row.address not in results:
            continue
        exact = results[row.address].exact
        unit = re.sub(r"\.(c|cpp)$", "", row.source.removeprefix("src/"))
        symbol = row.symbol if row.symbol.startswith("?") else "_" + row.symbol
        score = scores.get((unit, undecorate(symbol)))
        if score is None:
            if exact:
                owner = unit_at(row.address)
                if owner != row.source:
                    elsewhere.append((row.name, row.address, owner))
                elif len(names_at.get(row.address, ())) > 1:
                    aliases.append((row.name, sorted(names_at[row.address])))
                else:
                    renamed.append((row.name, sorted(names_at.get(row.address, ()))))
            continue
        if exact and score < 100:
            cut_short.append((row.name, score))
        elif not exact and score == 100:
            too_kind.append(row.name)
        else:
            agree += 1

    print(f"{version}: {agree} functions both judge alike")
    sections = [
        (
            cut_short,
            "exact, but objdiff's function is longer: code after it that functions.json does not list",
            lambda e: f"{e[0]} ({e[1]:.3f}%)",
        ),
        (
            too_kind,
            "100% in objdiff, not the original's: a reference to another variable or literal (tools/fn.py diff)",
            lambda e: e,
        ),
        (
            elsewhere,
            "exact, in another unit than its source: the original links it in that unit's object",
            lambda e: f"{e[0]} at 0x{e[1]:08x}: {e[2]}",
        ),
        (
            aliases,
            "exact; one of several names of one address (identical functions the linker kept once)",
            lambda e: f"{e[0]}: {', '.join(e[1])}",
        ),
        (
            renamed,
            "exact; symbols.txt names its address otherwise (another name of identical code, or a stale one)",
            lambda e: f"{e[0]}: {', '.join(e[1]) or 'no name'}",
        ),
    ]
    for entries, title, describe in sections:
        if entries:
            print(f"\n{len(entries)} {title}")
            for entry in entries:
                print("  " + describe(entry))


if __name__ == "__main__":
    main()
