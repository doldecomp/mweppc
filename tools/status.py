#!/usr/bin/env python3

import argparse
import bisect
import collections
import json
import signal
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from compare import link_progress
from mw import build, config, formats, project, verdict


def text_ranges(version):
    splits = formats.read_splits(project.version(version).config / "splits.txt")
    return sorted(
        ((u.name, r.start, r.end) for u in splits.units for r in u.ranges if r.section == ".text"), key=lambda u: u[1]
    )


def survey(version):
    v = project.version(version)
    cfg = config.version_config(v)
    sources = config.sources()
    results = verdict.load(v)
    functions = sorted(config.functions(v), key=lambda f: f.address)
    units = text_ranges(v)
    by_unit = collections.defaultdict(list)
    starts = [start for _, start, _ in units]
    for f in functions:
        i = bisect.bisect_right(starts, f.address) - 1
        if i >= 0 and f.address < units[i][2]:
            by_unit[units[i][0]].append(f)
    owned = collections.defaultdict(list)
    for f in functions:
        if f.source:
            owned[f.source].append(f)
    table = []
    for unit, start, end in units:
        inside = by_unit[unit]
        exact = [f for f in inside if results.get(f.address) and results[f.address].exact]
        if unit not in sources:
            link, compiler, flags = "-", "split", ""
        else:
            c = build.compilation(v, unit)
            compiler, flags = c.compiler, " ".join(c.flags)
            link = (
                "src"
                if cfg.linked(unit)
                else cfg.relink_exclude[unit].split(":")[0].split(";")[0][:40]
                if unit in cfg.matching
                else "-"
            )
        extent = {
            f.address: (inside[i + 1].address if i + 1 < len(inside) else end) - f.address for i, f in enumerate(inside)
        }
        size = end - start
        exact_bytes = size if link == "src" else sum(extent[f.address] for f in exact)
        table.append(
            dict(
                unit=unit,
                start=start,
                end=end,
                size=size,
                exact=len(exact),
                sourced=sum(f.source == unit for f in inside),
                total=len(inside),
                bytes=100 * exact_bytes / (size or 1),
                foreign=[(f"0x{f.address:08x}", f.source) for f in inside if f.source and f.source != unit],
                away=[f"0x{f.address:08x}" for f in owned.get(unit, []) if not start <= f.address < end],
                compiler=compiler,
                flags=flags,
                link=link,
            )
        )
    return table, by_unit, results


def state_of(f, unit, results):
    result = results.get(f.address)
    if result and result.exact:
        return "EXACT"
    if result and f.source == unit:
        return f"{100 * result.similarity:5.1f}" if result.similarity is not None else "?"
    return f"-> {f.source}" if f.source else "none"


def show_table(table):
    print(f"{'unit':<46} {'range':<17} {'fns':>11} {'bytes':>6} {'for':>3} {'awy':>3} {'compiler':<8} link")
    for t in table:
        print(
            f"{t['unit'][:46]:<46} {t['start']:06x}-{t['end']:06x} {t['exact']:>3}/{t['sourced']:>3}/{t['total']:>3}"
            f" {t['bytes']:5.1f}% {len(t['foreign']):>3} {len(t['away']):>3} {t['compiler']:<8} {t['link']}"
        )


def show_functions(table, by_unit, results, start=None, end=None):
    for t in table:
        print(f"\n== {t['unit']}  [{t['compiler']} {t['flags']}]  link: {t['link']}")
        for f in by_unit[t["unit"]]:
            if start is not None and not start <= f.address < end:
                continue
            state = state_of(f, t["unit"], results)
            result = results.get(f.address)
            reason = f"  ({result.reason})" if result and not result.exact and result.reason else ""
            print(f"  {f.address:06x} {f.size:6}  {state:<8} {f.name}{reason}")
        for address in t["away"]:
            print(f"  (away: {address})")


def todo(table, by_unit, results, cfg, limit):
    sources = config.sources()
    entries = []
    for t in table:
        unit = t["unit"]
        if unit not in sources or t["link"] == "src":
            continue
        inside = by_unit[unit]
        inexact = [f for f in inside if f.source == unit and not (results.get(f.address) and results[f.address].exact)]
        unsourced = [f for f in inside if not f.source]
        foreign = [f for f in inside if f.source and f.source != unit]
        blockers = []
        for f in sorted(inexact, key=lambda f: -f.size)[:4]:
            blockers.append(f"{f.name} {state_of(f, unit, results).strip()}")
        if len(inexact) > 4:
            blockers.append(f"+{len(inexact) - 4} inexact")
        if unsourced:
            blockers.append(f"{len(unsourced)} without source")
        if foreign:
            blockers.append(f"{len(foreign)} of other sources")
        if not inexact and not unsourced and not foreign:
            blockers.append("relink: " + cfg.relink_exclude.get(unit, "Matching once tools/update_state.py runs"))
        left = inexact + unsourced
        entries.append((sum(f.size for f in left), len(left), unit, sum(f.size for f in inside), blockers))
    entries.sort(key=lambda e: (e[0], e[1], -e[3]))
    print(f"{'unit':<44} {'adds':>6} {'left':>4} {'bytes':>6}  blocking")
    for left_bytes, left, unit, size, blockers in entries[: limit or None]:
        print(f"{unit[:44]:<44} {size:>6} {left:>4} {left_bytes:>6}  {'; '.join(blockers)}")
    print(
        f"\n{len(entries)} units with a source do not link from source; "
        f"{sum(e[1] == 0 for e in entries)} of them have every function exact"
    )


def hexadecimal(text):
    return int(text, 16)


def main():
    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    parser = argparse.ArgumentParser(
        description="Each translation unit of the version, in link order, with where it stands.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version", nargs="?", default=project.PRIMARY)
    where = parser.add_mutually_exclusive_group()
    where.add_argument("--unit", metavar="SUBSTRING", help="list the functions of the units whose name has SUBSTRING")
    where.add_argument(
        "--range",
        nargs=2,
        type=hexadecimal,
        metavar=("START", "END"),
        help="list the functions in [START, END) (hexadecimal)",
    )
    where.add_argument(
        "--todo",
        nargs="?",
        const=40,
        type=int,
        metavar="N",
        help="the N units nearest to linking (default 40; 0 for all)",
    )
    parser.add_argument("--json", metavar="FILE", help="also write the units' table as JSON")
    args = parser.parse_args()
    try:
        v = project.version(args.version, original=True)
        table, by_unit, results = survey(v)
        if args.json:
            project.write_atomic(args.json, json.dumps(table, indent=1) + "\n")
        if args.todo is not None:
            todo(table, by_unit, results, config.version_config(v), args.todo)
        elif args.unit:
            chosen = [t for t in table if args.unit in t["unit"]]
            if not chosen:
                raise project.Error(f"no unit's name has {args.unit!r}")
            show_functions(chosen, by_unit, results)
        elif args.range:
            start, end = args.range
            show_functions([t for t in table if t["start"] < end and t["end"] > start], by_unit, results, start, end)
        else:
            show_table(table)
            print(
                f"\n{v}: {link_progress(v, results)}; {sum(t['link'] == 'src' for t in table)} of {len(table)} "
                "units with code link from source"
            )
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
