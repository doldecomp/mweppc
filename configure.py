#!/usr/bin/env python3

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "tools"))
from mw import build, clang, config, formats, link, project

WINDOWS = sys.platform == "win32"
TOOL_DEP = "" if WINDOWS else "$wibo "
ENVIRONMENT = "cmd /c set MWCIncludes=include/libc&& " if WINDOWS else "MWCIncludes=include/libc "

STUB = " build/compilers/{}/lmgr326b.dll" if WINDOWS else ""
STUB_INPUT = " | tools/lmgr_stub/lmgr326b.dll" if WINDOWS else ""

UNBUILT = {"proof.py"}
LIBRARY = " ".join(
    sorted(project.relative(p) for p in (project.ROOT / "tools/mw").glob("*.py") if p.name not in UNBUILT)
)


def quote(argument):
    return subprocess.list2cmdline([argument]) if WINDOWS else shlex.quote(argument)


def join(arguments):
    return " ".join(map(quote, arguments))


def compile_edges(version):
    out, objects, debug_objects = [], [], []
    for c in build.compilations(version):
        obj = project.relative(build.object_path(version, c.source))
        executable = c.executable.replace("/", "\\") if WINDOWS else c.executable
        if c.pro4:
            out += [
                f"build {obj}: mwcc_pro4 {c.source} | {TOOL_DEP}{c.executable} || lib/ok.pro53",
                f"  cflags = {join(c.argv(obj))}",
            ]
        else:
            out += [
                f"build {obj}: mwcc {c.source} | {TOOL_DEP}{c.executable} || lib/ok.pro53",
                f"  cflags = {join(c.args)}",
                f"  stem = {Path(c.source).stem}",
                f"  pool = {pool(c.source)}",
            ]
        out.append(f"  compiler_command = {executable}")
        objects.append(obj)
        if not c.pro4:
            debug = project.relative(build.object_path(version, c.source, debug=True))
            out += [
                f"build {debug}: mwcc_debug {c.source} | {obj} {TOOL_DEP}{c.executable} || lib/ok.pro53",
                f"  cflags = {join(c.with_symbols().args)}",
                f"  compiler_command = {executable}",
            ]
            debug_objects.append(debug)
    return out, objects, debug_objects


def pool(source):
    return "stem_" + "".join(ch if ch.isalnum() else "_" for ch in Path(source).stem)


RESOURCES = "src/driver/mwcceppc.rc"


def version_edges(version, objects, objdiff, report=True):
    v = version.name
    cfg = config.version_config(version)
    configs = (
        " ".join(f"config/{v}/{name}" for name in ("config.json", "functions.json", "bindings.json"))
        + " config/sources.json"
    )
    link_inputs = (
        f"{configs} config/{v}/splits.txt tools/source_only.py {LIBRARY} {TOOL_DEP}{link.MWLD} "
        f"tools/lmgr_stub/lmgr326b.dll lib/ok.pro53 " + " ".join(link.RUNTIME_OBJECTS)
    )
    return [
        f"build build/{v}/valid: validate | {project.relative(cfg.original)} {configs} config/{v}/state.json "
        f"tools/validate.py {LIBRARY}",
        f"  version = {v}",
        f"build build/{v}/target/ok: extract | {project.relative(cfg.original)} {configs} tools/compare.py {LIBRARY}",
        f"  version = {v}",
        f"build build/{v}/ok build/{v}/compare.json: compare | build/{v}/valid build/{v}/target/ok {configs} "
        f"tools/compare.py "
        f"{LIBRARY} {' '.join(objects)}",
        f"  version = {v}",
        f"  objdiff = {'--objdiff' if objdiff else ''}",
        *(
            [f"build build/{v}/report.json: report | build/{v}/ok build/tools/objdiff-cli", f"  version = {v}"]
            if report
            else []
        ),
        f"build build/{v}/source-only/mwcceppc.exe: source_only {' '.join(objects)} | {link_inputs} "
        f"build/{v}/resources.res " + " ".join(link.source_import_library(name) for name in link.IMPORT_LIBRARIES),
        f"  version = {v}",
    ]


def derived_edges(version, objects):
    v, base = version.name, project.version(version.base)
    inputs = (
        " ".join(f"config/{base}/{name}" for name in ("config.json", "functions.json", "bindings.json", "splits.txt"))
        + f" config/{v}/config.json config/sources.json tools/source_only.py {LIBRARY} "
        f"{TOOL_DEP}{link.MWLD} tools/lmgr_stub/lmgr326b.dll lib/ok.pro53 "
        + " ".join([*link.RUNTIME_OBJECTS, *(link.source_import_library(name) for name in link.IMPORT_LIBRARIES)])
    )
    return [
        f"build build/{v}/source-only/mwcceppc.exe: source_only {' '.join(objects)} | {inputs} build/{v}/resources.res",
        f"  version = {v}",
    ]


def relink_edges(version, objects, debug_objects, selected):
    v = version.name
    cfg = config.version_config(version)
    sources = config.sources()
    splits = f"config/{v}/splits.txt"
    units = [u.name for u in formats.read_splits(project.ROOT / splits).units]
    split_objects = [link.split_object(v, unit) for unit in units if unit not in link.LIBRARY_OBJECTS]
    inputs = link.inputs(v, units, {unit for unit in units if unit in sources and cfg.linked(unit)})
    libraries = link.import_libraries(v)
    output = f"build/{v}/mwcceppc.exe"
    original = project.relative(cfg.original)
    out = [
        f"build build/{v}/layout.ok: layout_check | {original} build/{v}/compare.json config/{v}/functions.json "
        f"config/{v}/bindings.json config/{v}/config.json config/{v}/state.json config/sources.json "
        f"config/{v}/symbols.txt {splits} tools/layout.py {LIBRARY} {' '.join(debug_objects)}",
        f"  version = {v}",
        f"build build/{v}/obj/ok build/{v}/obj/sections.obj build/{v}/obj/resources.res {' '.join(split_objects)}: "
        f"split | {original} config/{v}/symbols.txt {splits} tools/split.py {LIBRARY}",
        f"  version = {v}",
        f"build {' '.join(libraries)}: implib | {original} config/{v}/symbols.txt tools/implib.py {LIBRARY}",
        f"  version = {v}",
        f"build {output} | build/{v}/relink.ok: relink {' '.join(inputs)} | {TOOL_DEP}{link.MWLD} tools/link.py "
        f"{LIBRARY} || lib/ok.pro53",
        f"  version = {v}",
    ]
    if not selected:
        return out
    write_objdiff(version, cfg, sources, units)
    bases = []
    for c in build.compilations(version):
        obj, debug = (project.relative(build.object_path(version, c.source, debug=d)) for d in (False, True))
        bases.append(objdiff_base(version, c.source))
        out += [f"build {bases[-1]}: objdiff_base {obj} {obj if c.pro4 else debug} | tools/compare.py {LIBRARY}"]
    return out + [
        f"build objdiff_bases: phony {' '.join(bases)}",
        f"build build/{v}/report.json: objdiff_report | build/{v}/relink.ok build/tools/objdiff-cli objdiff_bases",
        f"  version = {v}",
        f"build build/{v}/mwcceppc-debug.exe | build/{v}/mwcceppc-debug.pdb "
        f"build/{v}/mwcceppc-dwarf.exe build/{v}/mwcceppc-debug.txt: relink_debug {output} "
        f"{' '.join(debug_objects)} | config/{v}/functions.json config/{v}/symbols.txt {splits} "
        f"config/sources.json tools/link_debug.py "
        f"{LIBRARY}",
        f"  version = {v}",
    ]


def objdiff_base(version, source):
    return f"build/{version}/objdiff/{source}.obj"


def write_objdiff(version, cfg, sources, units):
    entries = []
    for unit in units:
        library = unit.startswith(("src/msl/", "src/runtime/")) or unit in link.LIBRARY_OBJECTS
        entry = dict(
            name=unit.removeprefix("src/").rsplit(".", 1)[0] if unit.startswith("src/") else unit,
            target_path=f"build/{version}/obj/{unit}.obj",
            metadata=dict(
                complete=unit in link.LIBRARY_OBJECTS, progress_categories=["runtime" if library else "compiler"]
            ),
        )
        if unit in sources:
            entry["base_path"] = objdiff_base(version, unit)
            entry["metadata"].update(complete=cfg.linked(unit), source_path=unit)
        elif unit in link.LIBRARY_OBJECTS:
            entry["base_path"] = link.LIBRARY_OBJECTS[unit]
            entry["target_path"] = link.LIBRARY_OBJECTS[unit]
        else:
            entry["metadata"]["auto_generated"] = not unit.startswith("unknown/")
        entries.append(entry)
    project.write_atomic(
        project.ROOT / "objdiff.json",
        json.dumps(
            {
                "$schema": "https://raw.githubusercontent.com/encounter/objdiff/main/config.schema.json",
                "min_version": "3.8.0",
                "custom_make": "ninja",
                "build_target": False,
                "build_base": True,
                "watch_patterns": [
                    "src/**/*.c",
                    "src/**/*.cpp",
                    "include/**/*.h",
                    "config/**/*.json",
                    "config/**/*.txt",
                ],
                "units": entries,
                "progress_categories": [
                    {"id": "compiler", "name": "Compiler"},
                    {"id": "runtime", "name": "MSL and runtime"},
                ],
            },
            indent=2,
        )
        + "\n",
    )


def resources_edge(version):
    return [
        f"build build/{version}/resources.res: rc {RESOURCES} | {RESOURCES.replace('.rc', '.ico')} include/version.h",
        f"  index = {version.index}",
    ]


CASE_STAMP = "build/clang/case/ok"


def clang_edges(version):
    out, objects = [], []
    for c in clang.compilations(version):
        obj = project.relative(clang.object_path(version, c.source))
        out += [
            f"build {obj}: clang {c.source} || {CASE_STAMP} lib/ok.pro53",
            f"  cflags = {join(clang.build_arguments(c)[1:])}",
        ]
        objects.append(obj)
    output = project.relative(clang.output_path(version))
    libraries = " ".join(link.source_import_library(name) for name in link.IMPORT_LIBRARIES)
    return out + [
        f"build {output} {output.removesuffix('.exe')}.map: clang_link {' '.join(objects)} | build/{version}/resources.res "
        f"{libraries} tools/clang_build.py {LIBRARY} tools/lmgr_stub/lmgr326b.dll",
        f"  version = {version}",
    ]


def native_edges(version):
    out, objects = [], []
    for c in clang.native_compilations(version):
        obj = project.relative(clang.native_object_path(version, c.source))
        resources = f" {clang.native_resources(version)}" if c.source == "src/host/linux/Start.c" else ""
        out += [
            f"build {obj}: clang {c.source} |{resources} || {CASE_STAMP} lib/ok.pro53",
            f"  cflags = {join(clang.native_arguments(c)[1:])}",
        ]
        objects.append(obj)
    output = project.relative(clang.native_output_path(version))
    return out + [
        f"build {output} {output}.map: native_link {' '.join(objects)} | tools/clang_build.py {LIBRARY}",
        f"  version = {version}",
    ]


def native_outputs():
    if not clang.native_supported():
        return []
    return [project.relative(clang.native_output_path(v)) for v in project.ALL_VERSIONS]


def clang_rules():
    return [
        "rule clang",
        "  command = clang $cflags -MD -MF $out.d -c $in -o $out",
        "  description = CLANG $in",
        "  depfile = $out.d",
        "  deps = gcc",
        "rule clang_link",
        "  command = $python tools/clang_build.py link $version",
        "  description = LINK (clang) $out",
        "rule native_link",
        "  command = $python tools/clang_build.py native $version",
        "  description = LINK (Linux) $out",
        "rule clang_case",
        "  command = $python tools/clang_build.py case $out",
        "  description = CASE $out",
        "rule rc",
        "  command = llvm-rc -I include -D VERSION=$index -fo $out $in",
        "  description = RC $out",
        "rule dlltool",
        "  command = llvm-dlltool -m i386 -k -d $in -l $out",
        "  description = DLLTOOL $out",
    ]


def clang_build_edges():
    return [
        "build lib/ok.pro53 " + " ".join(link.RUNTIME_OBJECTS) + ": download",
        "  tool = lib",
        f"build {CASE_STAMP}: clang_case | lib/ok.pro53 tools/clang_build.py {LIBRARY}",
        *(
            f"build {link.source_import_library(name)}: dlltool {link.import_definition(name)}"
            for name in link.IMPORT_LIBRARIES
        ),
    ]


def configure_command(versions, mwcc, relink):
    all_versions = list(project.ALL_VERSIONS) if not mwcc else [project.version()]
    arguments = [] if versions == all_versions else ["--version", ",".join(v.name for v in versions)]
    arguments += ["--matching"] if relink else ["--mwcc"] if mwcc else []
    return " ".join(["$python configure.py", *arguments])


def generate_clang(versions):
    out = [
        "# Generated by configure.py",
        f"python = {quote(sys.executable)}",
        "",
        "pool downloads",
        "  depth = 1",
        "rule download",
        "  command = $python tools/download_tool.py $tool $out",
        "  description = DOWNLOAD $out",
        "  pool = downloads",
        *clang_rules(),
        "rule configure",
        f"  command = {configure_command(versions, False, False)}",
        "  generator = 1",
        "",
        *clang_build_edges(),
    ]
    for v in project.ALL_VERSIONS:
        out += resources_edge(v) + clang_edges(v)
        if clang.native_supported():
            out += native_edges(v)
    outputs = [project.relative(clang.output_path(v)) for v in versions]
    if clang.native_supported():
        outputs += [project.relative(clang.native_output_path(v)) for v in versions]
    out += [
        f"default {' '.join(outputs)}",
        "build clang: phony " + " ".join(project.relative(clang.output_path(v)) for v in project.ALL_VERSIONS),
        *([f"build native: phony {' '.join(native_outputs())}"] if native_outputs() else []),
        f"build build.ninja: configure | configure.py {LIBRARY} config/sources.json include/version.h "
        + " ".join(f"config/{v}/config.json" for v in project.ALL_VERSIONS),
        "",
    ]
    return "\n".join(out)


def generate(versions, relink):
    selected = versions[0]
    sources = config.sources()
    out = [
        "# Generated by configure.py",
        f"python = {quote(sys.executable)}",
        "wibo = " + ("" if WINDOWS else "build/tools/wibo"),
        "",
        "pool downloads",
        "  depth = 1",
        *(line for stem in sorted({pool(s) for s in sources}) for line in (f"pool {stem}", "  depth = 1")),
        "rule download",
        "  command = $python tools/download_tool.py $tool $out",
        "  description = DOWNLOAD $out",
        "  pool = downloads",
        "rule mwcc",
        "  command = " + ENVIRONMENT + "$wibo $compiler_command $cflags -MD $in -o $out"
        " && $python tools/transform_dep.py $stem.dep $out",
        "  description = MWCC $in",
        "  depfile = $out.d",
        "  deps = gcc",
        "rule mwcc_debug",
        "  command = " + ENVIRONMENT + "$wibo $compiler_command $cflags $in -o $out",
        "  description = MWCC (debug) $in",
        "rule mwcc_pro4",
        "  command = " + ENVIRONMENT + "$wibo $compiler_command $cflags",
        "  description = MWCC $in",
        "rule source_only",
        "  command = $python tools/source_only.py $version",
        "  description = LINK (source only) $out",
        *clang_rules(),
        "rule validate",
        "  command = $python tools/validate.py --stamp $out $version",
        "  description = VALIDATE $version",
        "rule extract",
        "  command = $python tools/compare.py extract $version",
        "  description = EXTRACT $version",
        "rule compare",
        "  command = $python tools/compare.py compare $version $objdiff",
        "  description = COMPARE $version",
        "rule report",
        "  command = $python tools/compare.py report $version",
        "  description = REPORT $version",
        "rule layout_check",
        "  command = $python tools/layout.py --check --stamp $out $version",
        "  description = LAYOUT $version",
        "rule split",
        "  command = $python tools/split.py $version",
        "  description = SPLIT $version",
        "rule implib",
        "  command = $python tools/implib.py $version build/$version/implib",
        "  description = IMPLIB $version",
        "rule relink",
        "  command = $python tools/link.py $version $out $in",
        "  description = RELINK $out",
        "rule relink_debug",
        "  command = $python tools/link_debug.py $version $out",
        "  description = DEBUG $out",
        "rule objdiff_base",
        "  command = $python tools/compare.py base $in $out",
        "  description = BASE $out",
        "rule objdiff_report",
        "  command = build/tools/objdiff-cli report generate -o $out",
        "  description = REPORT $version",
        "rule configure",
        f"  command = {configure_command(versions, True, relink)}",
        "  generator = 1",
        "",
        "build build/tools/objdiff-cli: download",
        "  tool = objdiff-cli",
        *([] if WINDOWS else ["build build/tools/wibo: download", "  tool = wibo"]),
        "build build/compilers/ok "
        + " ".join(f"build/compilers/GC/{v.name[3:].replace('_', '.')}/mwcceppc.exe" for v in project.VERSIONS)
        + ": download",
        "  tool = compilers",
        *clang_build_edges(),
        f"build {link.MWLD}: download",
        "  tool = pro53_mwld",
    ]
    compilers = sorted({c.compiler for v in project.VERSIONS for c in build.compilations(v)})
    for compiler in compilers:
        stub = WINDOWS and compiler != "pro4"
        out += [
            f"build build/compilers/{compiler}/mwcc.exe{STUB.format(compiler) if stub else ''}: download"
            + (STUB_INPUT if stub else ""),
            f"  tool = {compiler}",
        ]
    selected_objects, selected_debug = [], []
    for v in project.VERSIONS:
        edges, objects, debug_objects = compile_edges(v)
        out += edges + resources_edge(v)
        out += version_edges(v, objects, objdiff=v == selected and not relink, report=not (relink and v == selected))
        if (v.config / "splits.txt").exists():
            out += relink_edges(v, objects, debug_objects, selected=relink and v == selected)
        if v == selected:
            selected_objects, selected_debug = objects, debug_objects
    for v in project.DERIVED:
        edges, objects, _ = compile_edges(v)
        out += edges + resources_edge(v) + derived_edges(v, objects)
    for v in project.ALL_VERSIONS:
        out += clang_edges(v)
        if clang.native_supported():
            out += native_edges(v)
    defaults = [f"build/{v}/ok" for v in versions]
    regenerate = [
        "configure.py",
        LIBRARY,
        "config/sources.json",
        "include/version.h",
        *(f"config/{v}/{name}" for v in project.VERSIONS for name in ("config.json", "state.json")),
        *(f"config/{v}/config.json" for v in project.DERIVED),
        *(f"config/{v}/splits.txt" for v in project.VERSIONS if (v.config / "splits.txt").exists()),
    ]
    if relink:
        defaults += [f"build/{selected}/relink.ok", f"build/{selected}/mwcceppc-debug.exe", "objdiff_bases"]
    out += [
        f"default {' '.join(defaults)}",
        f"build all_source: phony {' '.join(selected_objects)}",
        f"build all_debug: phony {' '.join(selected_debug)}",
        f"build progress: phony build/{selected}/report.json",
        "build clang: phony " + " ".join(project.relative(clang.output_path(v)) for v in project.ALL_VERSIONS),
        *([f"build native: phony {' '.join(native_outputs())}"] if native_outputs() else []),
        "build verify: phony "
        + " ".join(f"build/{v}/ok build/{v}/source-only/mwcceppc.exe" for v in project.VERSIONS)
        + " "
        + " ".join(f"build/{v}/source-only/mwcceppc.exe" for v in project.DERIVED)
        + " "
        + " ".join(
            f"build/{v}/{target}"
            for v in project.VERSIONS
            if (v.config / "splits.txt").exists()
            for target in ("layout.ok", "relink.ok")
        ),
        f"build build.ninja: configure | {' '.join(regenerate)}",
        "",
    ]
    return "\n".join(out)


def parse_versions(text, mwcc):
    if text is None:
        return [project.version()] if mwcc else list(project.ALL_VERSIONS)
    versions = []
    for name in text.split(","):
        if not name.strip():
            raise project.Error(f"--version {text!r}: a version's name is empty")
        v = project.version(name.strip(), original=mwcc)
        if v not in versions:
            versions.append(v)
    return versions


def main():
    parser = argparse.ArgumentParser(
        description="Write build.ninja and compile_commands.json.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument(
        "--version",
        metavar="VERSION[,VERSION...]",
        help=f"the versions to build (one of {', '.join(v.name for v in project.ALL_VERSIONS)}; by default all, with "
        f"--mwcc {project.PRIMARY}); with --mwcc, objdiff and --matching take the first",
    )
    parser.add_argument(
        "--mwcc",
        action="store_true",
        help="the CodeWarrior workflow: the sources built with the CodeWarrior compilers and compared with the "
        "originals, the source-only compilers, ninja verify",
    )
    parser.add_argument(
        "--matching",
        action="store_true",
        help="also split the original and relink it, checked against its SHA-1 (implies --mwcc)",
    )
    arguments = parser.parse_args()
    mwcc = arguments.mwcc or arguments.matching
    try:
        versions = parse_versions(arguments.version, mwcc)
        selected = versions[0]
        if arguments.matching and not (selected.config / "splits.txt").exists():
            sys.exit(f"config/{selected}/splits.txt does not exist: python tools/layout.py {selected}")
        text = generate(versions, arguments.matching) if mwcc else generate_clang(versions)
        project.write_atomic(project.ROOT / "build.ninja", text)
        primary = project.version()
        clang.write_compile_commands(primary if primary in versions else selected)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
