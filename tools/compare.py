#!/usr/bin/env python3
"""python tools/compare.py {extract,compare [--objdiff],report} VERSION: the comparison ninja runs.

extract  the original's functions and the ranges between them as one-section objects (build/VERSION/target), for
         objdiff's per-function view
compare  each function of the sources against the original (tools/mw/verdict.py: the one definition of exact); writes
         build/VERSION/compare.json (every verdict with its reason, stamped with what it was computed from) whatever the
         outcome, the per-function objects and their objdiff project, build/VERSION/objdiff.json (with --objdiff also
         the repository's objdiff.json), and build/VERSION/ok when no function of a Matching source differs (else it
         fails, listing them)
report   objdiff's progress report of build/VERSION/objdiff.json (build/VERSION/report.json)

python tools/compare.py base OBJECT DEBUG OUTPUT: objdiff's base object of a source in the relink's objdiff.json: the
object compiled with symbols on (DEBUG, whose line numbers objdiff shows) when its code is OBJECT's, else OBJECT
(symbols on changes some sources' code).
"""

import json
import re
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import coff, config, isolated, project, verdict
from mw.pe import original


def segments(version):
    """[(name, address, size, code, row or None)] over the original's code and data: each row, and the ranges between."""
    pe = original(version)
    rows = verdict.rows(version)
    out = []
    for section in pe.sections:
        if not section.code and section.name in (".reloc", ".rsrc", ".idata", ".edata"):
            continue
        start, end = section.virtual_address, section.virtual_address + section.virtual_size
        cursor = start
        inside = sorted((r for r in rows if start <= r.address < end), key=lambda r: r.address) if section.code else []
        for row in inside:
            if row.address > cursor:
                out.append((f"unknown/{section.name}/{cursor:08x}", cursor, row.address - cursor, section.code, None))
            unit = Path(row.source).with_suffix("").as_posix() if row.source else "unrecovered"
            out.append((f"{unit}/{row.symbol}", row.address, row.size, True, row))
            cursor = row.address + row.size
        if cursor < end:
            out.append((f"unknown/{section.name}/{cursor:08x}", cursor, end - cursor, section.code, None))
    return out


def object_path(version, kind, name):
    # (Windows file names cannot hold these)
    if sys.platform == "win32":
        name = re.sub(r'[<>:"|?*]', lambda m: f"%{ord(m[0]):02x}", name)
    return version.build / kind / f"{name}.obj"


def write_one(path, data, symbol, code, bss_size=0):
    """A one-section object (an unknown range gets no symbol: it is not a function)."""
    obj = coff.Object()
    if code:
        flags = coff.CODE | coff.EXECUTE | coff.READ
        obj.add_section(".text", flags, data, section_symbol=False)
    elif bss_size:
        obj.add_section(".bss", coff.UNINITIALIZED | coff.READ | coff.WRITE, b"", bss_size, section_symbol=False)
    else:
        obj.add_section(".data", coff.INITIALIZED | coff.READ | coff.WRITE, data, section_symbol=False)
    if symbol:
        obj.add_symbol(symbol, 0, 1, 0x20 if code else 0, coff.EXTERNAL)
    obj.write(path)


def linker_symbol(row):
    return row.symbol if row.symbol.startswith("?") else "_" + row.symbol


def extract(version):
    pe = original(version)
    for name, address, size, code, row in segments(version):
        section = pe.section_for_address(address)
        data = b"" if section.bss else pe.read(address, size)
        write_one(
            object_path(version, "target", name),
            data,
            linker_symbol(row) if row else (f"__unknown_{address:08x}" if code else None),
            code,
            size if section.bss else 0,
        )
    (version.build / "target/ok").touch()


def link_progress(version, results):
    """The headline: how much of the original the --matching build takes from source (a function counts once it is
    exact and its source links)."""
    cfg = config.version_config(version)
    rows = verdict.rows(version)
    done = [
        r for r in rows if r.source and cfg.linked(r.source) and results.get(r.address) and results[r.address].exact
    ]
    size, done_size = sum(r.size for r in rows), sum(r.size for r in done)
    return (
        f"linked {len(done)}/{len(rows)} functions ({100 * len(done) / len(rows):.1f}%), "
        f"{done_size}/{size} bytes ({100 * done_size / size:.1f}%), {len({r.source for r in done})} sources"
    )


def compare(version, objdiff=False):
    cfg = config.version_config(version)
    results = verdict.compare(version)
    verdict.save(version, results)
    units, failed, nonmatching = [], [], []
    for name, address, size, code, row in segments(version):
        meta = {"complete": False, "progress_categories": ["code" if code else "data"]}
        unit = dict(name=name, target_path=project.relative(object_path(version, "target", name)), metadata=meta)
        units.append(unit)
        if row is None or not row.source:
            continue
        meta["source_path"] = row.source
        v = results[row.address]
        if v.body is not None:
            base = object_path(version, "base", name)
            write_one(base, v.body, linker_symbol(row), True)
            unit["base_path"] = project.relative(base)
        if v.exact:
            meta["complete"] = cfg.linked(row.source)
        elif row.source in cfg.matching:
            failed.append(v)
        else:
            nonmatching.append(v)
    write_objdiff(version.build / "objdiff.json", units)
    if objdiff:
        write_objdiff(project.ROOT / "objdiff.json", units)
    exact = sum(v.exact for v in results.values())
    summary = [f"{version}: {link_progress(version, results)}; {exact}/{len(results)} functions exact"]
    if nonmatching:
        summary.append(
            f"  {len(nonmatching)} functions of NonMatching sources are not exact yet "
            f"(build/{version}/compare.json, tools/status.py)"
        )
    summary += [f"  not exact: {v.source}: {v.name}: {v.reason}" for v in failed]
    print("\n".join(summary))
    ok = version.build / "ok"
    if failed:
        ok.unlink(missing_ok=True)
        sys.exit(1)
    project.write_atomic(ok, "\n".join(summary) + "\n")


def write_objdiff(path, units):
    """objdiff's project of the per-function objects UNITS (their paths relative to the root)."""
    project.write_atomic(
        path,
        json.dumps(
            {
                "$schema": "https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json",
                "min_version": "3.8.0",
                "custom_make": "ninja",
                "build_target": False,
                # (the per-function base objects are this comparison's output, not ninja targets: objdiff cannot build one.
                # For objdiff itself use the --matching build's objdiff.json, a unit per translation unit, built by ninja.)
                "build_base": False,
                "watch_patterns": ["src/**/*.c", "src/**/*.cpp", "include/**/*.h", "config/**/*.json"],
                "units": units,
                "progress_categories": [{"id": "code", "name": "Code"}, {"id": "data", "name": "Data"}],
            },
            indent=2,
        )
        + "\n",
    )


def report(version):
    """objdiff's report of the version's per-function project (the repository's objdiff.json may be another version's,
    or the relink's): from a copy of it with absolute paths, as objdiff reads a project's paths from its directory."""
    source = version.build / "objdiff.json"
    if not source.exists():
        raise project.Error(f"{project.relative(source)} does not exist: run ninja build/{version}/ok")
    data = json.loads(source.read_text())
    for unit in data["units"]:
        for key in ("target_path", "base_path"):
            if key in unit:
                unit[key] = str(project.ROOT / unit[key])
    directory = version.build / "report"
    project.write_atomic(directory / "objdiff.json", json.dumps(data, indent=2) + "\n")
    out = version.build / "report.json"
    objdiff = str(project.ROOT / "build/tools/objdiff-cli")
    command = [objdiff, "report", "generate", "-p", str(directory), "-o", str(out)]
    try:
        failed = subprocess.run(command, cwd=project.ROOT).returncode
    except FileNotFoundError:
        raise project.Error("build/tools/objdiff-cli does not exist: ninja build/tools/objdiff-cli") from None
    if failed:
        raise project.Error(f"objdiff-cli failed on {project.relative(directory / 'objdiff.json')}")
    data = json.loads(out.read_text())
    # (an unknown code range carries a symbol only so objdiff keeps its bytes: it is not a function)
    removed = 0
    for unit in data["units"]:
        if unit["name"].startswith("unknown/"):
            removed += unit["measures"].get("total_functions", 0)
            unit.pop("functions", None)
            unit["measures"]["total_functions"] = 0
            unit["measures"]["matched_functions_percent"] = 0.0
    for measures in [data["measures"], *(c["measures"] for c in data.get("categories", []) if c["id"] == "code")]:
        measures["total_functions"] = measures.get("total_functions", 0) - removed
        total = measures["total_functions"]
        measures["matched_functions_percent"] = 100.0 * measures.get("matched_functions", 0) / total if total else 0.0
    project.write_atomic(out, json.dumps(data, indent=2) + "\n")


def base(obj, debug, output):
    chosen = debug if isolated.same_object(obj, debug) else obj
    project.write_atomic(output, Path(chosen).read_bytes())


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    args = sys.argv[1:]
    if args[:1] == ["base"] and len(args) == 4:
        base(*args[1:])
        return
    objdiff = "--objdiff" in args
    args = [a for a in args if a != "--objdiff"]
    if len(args) != 2 or args[0] not in ("extract", "compare", "report"):
        sys.exit(__doc__)
    try:
        version = project.version(args[1], original=True)
        if args[0] == "compare":
            compare(version, objdiff)
        else:
            {"extract": extract, "report": report}[args[0]](version)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
