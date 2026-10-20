#!/usr/bin/env python3

import argparse
import bisect
import concurrent.futures
import subprocess
import sys
import tempfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from compare import link_progress
from mw import build, config, formats, link, project, verdict
from mw.coff import Object
from mw.pe import original


class Relink:
    def __init__(self, version, directory):
        self.version = project.version(version)
        self.cfg = config.version_config(self.version)
        self.sources = config.sources()
        splits = formats.read_splits(self.version.config / "splits.txt")
        self.units = [u.name for u in splits.units]
        self.ranges = {}
        for unit in splits.units:
            ranges = self.ranges.setdefault(unit.name, {})
            for r in unit.ranges:
                if not r.option("common"):
                    ranges.setdefault(r.option("rename", r.section).split("$")[0], []).append((r.start, r.end))
        self.symbols = formats.read_symbols(self.version.config / "symbols.txt")
        self.common_start = self.ranges.get("common", {}).get(".bss", [(1 << 32, 0)])[0][0]
        self.output = Path(directory) / f"{self.version}.exe"
        self.pe = original(self.version)

    def link(self, compiled):
        result = link.mwld(link.inputs(self.version, self.units, compiled), self.output)
        if result.returncode:
            return "the link fails: " + "; ".join(link.errors(result)[:2])
        linked = link.with_original_stamp(self.output.read_bytes(), self.pe)
        return None if project.digest(linked) == self.cfg.sha1 else "links, but not to the original"

    def first_differences(self, count=6):
        linked = self.output.read_bytes()
        data = self.pe.data
        named = sorted((s.address, s.name) for s in self.symbols)
        found, total, previous = [], abs(len(linked) - len(data)), -2
        for offset in range(min(len(linked), len(data))):
            if linked[offset] == data[offset]:
                continue
            total += 1
            if offset != previous + 1 and len(found) < count:
                section = next(
                    (s for s in self.pe.sections if s.file_offset <= offset < s.file_offset + s.file_size), None
                )
                if section is None:
                    found.append(f"offset {offset:#x} (headers)")
                else:
                    address = section.virtual_address + offset - section.file_offset
                    index = bisect.bisect_right(named, (address, "￿")) - 1
                    name = f"{named[index][1]}+{address - named[index][0]:#x}" if index >= 0 else "?"
                    found.append(f"{address:#x} ({section.name} {name})")
            previous = offset
        return found, total

    def differences(self, unit):
        obj = Object.read(build.object_path(self.version, unit))
        ranges = self.ranges.get(unit, {})
        out = []
        text = ranges.get(".text", [(0, 0)])[0]
        original = [
            s.name
            for s in sorted(self.symbols, key=lambda s: s.address)
            if s.section == ".text" and text[0] <= s.address < text[1]
        ]
        defined, local = [], set()
        for section in obj.sections:
            if section.code:
                functions = sorted(
                    (s for s in obj.symbols if s.section == section.number and s.function and s.storage in (2, 3)),
                    key=lambda s: s.value,
                )
                defined += [s.name for s in functions]
                local |= {s.name for s in functions if s.storage == 3}
        everywhere = {s.name for s in self.symbols if s.section == ".text"}
        extra = [n for n in defined if n not in original and n not in local and n in everywhere]
        missing = [n for n in original if n not in defined]
        if extra:
            out.append(f"defines {len(extra)} function(s) of other units ({', '.join(extra[:3])})")
        if missing:
            out.append(f"lacks {len(missing)} of the unit's functions ({', '.join(missing[:3])})")
        kept = [n for n in defined if n in original or (n not in local and n in everywhere)]
        if not extra and not missing and original not in (kept, kept[::-1]):
            out.append("defines its functions in another order than the original")
        dropped = {n for n in defined if n not in original}

        def dead(section):
            targets = [obj.by_index.get(r.symbol) for r in section.relocations]
            return bool(targets) and all(t and t.function and t.name in dropped for t in targets)

        for name in (".data", ".rdata", ".exc", ".bss", ".tls"):
            size = 0
            for section in obj.sections:
                if section.name.split("$")[0] == name and not dead(section):
                    size = -(-size // section.alignment) * section.alignment + section.size
            expected = sum(end - start for start, end in ranges.get(name, []))
            if size != expected and not 0 < size <= expected < size + 8:
                out.append(f"{name} is {size} bytes, the unit's {expected}")
        addresses = {s.name: s.address for s in self.symbols}
        statics = [
            s.name for s in obj.symbols if s.common and addresses.get(s.name, self.common_start) < self.common_start
        ]
        if statics:
            out.append(f"declares as COMMON what the original has in an object's .bss ({', '.join(statics[:3])})")
        return "; ".join(out)

    def why(self, unit, failure):
        detail = self.differences(unit)
        return f"{failure}{': ' + detail if detail else ''}"

    def solve(self):
        candidates = [u for u in self.units if u in self.sources and u in self.cfg.matching]
        linked, excluded = set(), {}
        remaining = candidates
        while remaining:
            failed = []
            for unit in remaining:
                failure = self.link(linked | {unit})
                if failure is None:
                    linked.add(unit)
                    excluded.pop(unit, None)
                else:
                    failed.append(unit)
                    excluded[unit] = self.why(unit, failure)
            remaining = failed if len(failed) < len(remaining) else []
        if self.link(linked) is not None:
            raise project.Error(f"{self.version}: the {len(linked)} sources that link one by one do not link together")
        return linked, excluded, len(candidates)


def bring_up_to_date(versions):
    targets = []
    for v in versions:
        targets += [f"build/{v}/compare.json", f"build/{v}/obj/ok", *link.import_libraries(v)]
    result = subprocess.run(["ninja", *targets], cwd=project.ROOT, capture_output=True, text=True)
    if result.returncode:
        raise project.Error(f"ninja {' '.join(targets[:3])} ... failed:\n{(result.stdout + result.stderr)[-3000:]}")


def solve(version):
    with tempfile.TemporaryDirectory(dir=project.ROOT / "build") as directory:
        relink = Relink(version, directory)
        linked, excluded, count = relink.solve()
    return linked, excluded, count


def main():
    parser = argparse.ArgumentParser(
        description="Which Matching sources' compiled objects link into the original (default: every version with a layout).",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("versions", nargs="*", metavar="VERSION")
    parser.add_argument("--only", nargs="+", metavar="SOURCE", help="try these sources; write nothing")
    args = parser.parse_args()
    try:
        versions = [project.version(v, original=True) for v in args.versions] or [
            v for v in project.VERSIONS if (v.config / "splits.txt").exists()
        ]
        if args.only:
            if len(versions) != 1:
                raise project.Error("--only takes one version")
            bring_up_to_date(versions)
            version = versions[0]
            with tempfile.TemporaryDirectory(dir=project.ROOT / "build") as directory:
                relink = Relink(version, directory)
                for unit in args.only:
                    if unit not in relink.units:
                        raise project.Error(f"{unit} is not a unit of config/{version}/splits.txt")
                base = {u for u in relink.units if u in relink.sources and relink.cfg.linked(u) and u not in args.only}
                for unit in args.only:
                    failure = relink.link(base | {unit})
                    detail = relink.differences(unit)
                    print(f"{unit}: {failure or 'links into the original'}{': ' + detail if detail else ''}")
                    if failure and failure.startswith("links"):
                        found, total = relink.first_differences()
                        print(f"  {total} bytes differ, first at " + ", ".join(found))
            return
        bring_up_to_date(versions)
        with concurrent.futures.ProcessPoolExecutor() as pool:
            results = dict(zip(versions, pool.map(solve, versions)))
        with project.Transaction() as tx:
            for v, (linked, excluded, count) in results.items():
                path = v.config / "state.json"
                now = config.state(v, tx.read(path))
                tx.write(
                    path,
                    config.dump_state(now["matching"], {u: why for u, why in excluded.items() if u in now["matching"]}),
                )
        for v, (linked, excluded, count) in results.items():
            print(f"{v}: {len(linked)} of {count} Matching sources link into the original")
            print(f"  {link_progress(v, verdict.load(v))}")
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
