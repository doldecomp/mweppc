#!/usr/bin/env python3
"""python configure.py [--version VERSION[,VERSION...]] [--mwcc] [--matching]: write build.ninja (and
compile_commands.json, the clang build's command for each source, for clangd: tools/mw/clang.py).

By default `ninja` builds every version's compiler with clang, from the sources alone
(build/VERSION/clang/mwcceppc.exe: tools/mw/clang.py says how), and needs nothing but clang, lld-link, llvm-rc,
llvm-dlltool and the MSL and runtime sources it downloads (lib/). --version restricts it to those versions.

With --mwcc, build.ninja is the CodeWarrior workflow's: every source compiled by the CodeWarrior compilers the
original was built with (under wibo off Windows) and compared with the original (tools/mw/build.py says how each
source is compiled), the source-only compiler (build/VERSION/source-only/mwcceppc.exe: the sources' objects linked
with the original's linker), and `ninja verify` for every version; plain `ninja` builds and compares the VERSIONs
(default GC_2_6), and objdiff.json is the first's. The clang build is there too (`ninja clang`).

With --matching (which implies --mwcc; a version with config/VERSION/splits.txt), `ninja` also builds VERSION as
decomp-toolkit projects do: the original is split into one object per translation unit (tools/split.py), linked in the
original's order with the original's linker from the compiled objects of the units that link (Matching, not excluded
by tools/relink.py) and the split objects of the others, and checked against the original's SHA-1
(build/VERSION/mwcceppc.exe and build/VERSION/relink.ok); objdiff.json then has a unit per translation unit.
"""

import argparse
import json
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent / "tools"))
from mw import build, clang, config, formats, link, project

# (the compilers are Windows executables: wibo runs them elsewhere, and cmd.exe runs ninja's commands on Windows)
WINDOWS = sys.platform == "win32"
TOOL_DEP = "" if WINDOWS else "$wibo "
ENVIRONMENT = "cmd /c set MWCIncludes=include/libc&& " if WINDOWS else "MWCIncludes=include/libc "

# (the Pro 5 compilers check a FlexLM license: tools/lmgr_stub stands in for the library, beside each of them)
STUB = " build/compilers/{}/lmgr326b.dll" if WINDOWS else ""
STUB_INPUT = " | tools/lmgr_stub/lmgr326b.dll" if WINDOWS else ""

# (what every Python step depends on besides its own script: the library, but for the modules no step imports, the
# equivalence prover's: editing them rebuilds nothing)
UNBUILT = {"proof.py"}
LIBRARY = " ".join(
    sorted(project.relative(p) for p in (project.ROOT / "tools/mw").glob("*.py") if p.name not in UNBUILT)
)


def quote(argument):
    return subprocess.list2cmdline([argument]) if WINDOWS else shlex.quote(argument)


def join(arguments):
    return " ".join(map(quote, arguments))


def compile_edges(version):
    """Each source's object (and its object with symbols on, for the debug executables)."""
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
            # (-MD writes <stem>.dep to the working directory: compiles of one source run one at a time)
            out += [
                f"build {obj}: mwcc {c.source} | {TOOL_DEP}{c.executable} || lib/ok.pro53",
                f"  cflags = {join(c.args)}",
                f"  stem = {Path(c.source).stem}",
                f"  pool = {pool(c.source)}",
            ]
        out.append(f"  compiler_command = {executable}")
        objects.append(obj)
        if not c.pro4:
            # (with symbols on: CodeView for the debug executables. It is the matching object's code only where the
            # original's flags have symbols on, or where turning them on changes nothing: tools/link_debug.py)
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


# (the driver's resources: its version information and icon)
RESOURCES = "src/driver/mwcceppc.rc"


def version_edges(version, objects, objdiff, report=True):
    """The comparison and the source-only compiler of one version (and objdiff's report, unless the relink makes it)."""
    v = version.name
    cfg = config.version_config(version)
    configs = (
        " ".join(f"config/{v}/{name}" for name in ("config.json", "functions.json", "bindings.json"))
        + " config/sources.json"
    )
    # (the source-only compiler reads the rows, the bindings and the machine models' order in splits.txt)
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
        # (the compiler linked from the sources alone: ninja build/VERSION/source-only/mwcceppc.exe)
        f"build build/{v}/source-only/mwcceppc.exe: source_only {' '.join(objects)} | {link_inputs} "
        f"build/{v}/resources.res " + " ".join(link.source_import_library(name) for name in link.IMPORT_LIBRARIES),
        f"  version = {v}",
    ]


def derived_edges(version, objects):
    """A derived version's compiler, linked from its objects alone (ninja build/VERSION/source-only/mwcceppc.exe), with
    its base's configuration."""
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
    """The split, link and SHA-1 check of the original (build/VERSION/mwcceppc.exe, build/VERSION/relink.ok); for the
    selected version also objdiff.json, objdiff's report and the original with debugging information
    (build/VERSION/mwcceppc-debug.exe and .pdb, mwcceppc-dwarf.exe: tools/link_debug.py)."""
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
        # (the layout follows the compiled objects: checked, not rewritten (tools/layout.py writes it))
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
        # (the original with what the objects compiled with symbols on say of it, as a PDB and as DWARF)
        f"build build/{v}/mwcceppc-debug.exe | build/{v}/mwcceppc-debug.pdb "
        f"build/{v}/mwcceppc-dwarf.exe build/{v}/mwcceppc-debug.txt: relink_debug {output} "
        f"{' '.join(debug_objects)} | config/{v}/functions.json config/{v}/symbols.txt {splits} "
        f"config/sources.json tools/link_debug.py "
        f"{LIBRARY}",
        f"  version = {v}",
    ]


def objdiff_base(version, source):
    """objdiff's base object of SOURCE (tools/compare.py base: with line numbers where symbols on keep the code)."""
    return f"build/{version}/objdiff/{source}.obj"


def write_objdiff(version, cfg, sources, units):
    """objdiff's project: each unit's split object (target), and its compiled one (base) when it has a source."""
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
            # (complete: the compiled object is the one the link takes, and links into the original)
            entry["metadata"].update(complete=cfg.linked(unit), source_path=unit)
        elif unit in link.LIBRARY_OBJECTS:
            entry["base_path"] = link.LIBRARY_OBJECTS[unit]
            entry["target_path"] = link.LIBRARY_OBJECTS[unit]
        else:
            # (the build stamp, import thunks, the linker's COMMON block, units without a source yet)
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
    """The driver's resources (llvm-rc), which the source-only and clang builds link."""
    return [
        f"build build/{version}/resources.res: rc {RESOURCES} | {RESOURCES.replace('.rc', '.ico')} include/version.h",
        f"  index = {version.index}",
    ]


# (the lowercase links to lib/'s headers a case-sensitive file system needs: tools/clang_build.py case)
CASE_STAMP = "build/clang/case/ok"


def clang_edges(version):
    """The compiler built with clang: each source's object and their link (ninja build/VERSION/clang/mwcceppc.exe)."""
    out, objects = [], []
    for c in build.compilations(version):
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


def clang_rules():
    return [
        # (clang, lld-link and LLVM's tools from the PATH)
        "rule clang",
        "  command = clang $cflags -MD -MF $out.d -c $in -o $out",
        "  description = CLANG $in",
        "  depfile = $out.d",
        "  deps = gcc",
        "rule clang_link",
        "  command = $python tools/clang_build.py link $version",
        "  description = LINK (clang) $out",
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
    """What every version's clang build shares: lib/ (MSL and the runtime), the lowercase links to its headers and the
    import libraries."""
    return [
        "build lib/ok.pro53 " + " ".join(link.RUNTIME_OBJECTS) + ": download",
        "  tool = lib",
        f"build {CASE_STAMP}: clang_case | lib/ok.pro53 tools/clang_build.py {LIBRARY}",
        # (the import libraries, from src/imports)
        *(
            f"build {link.source_import_library(name)}: dlltool {link.import_definition(name)}"
            for name in link.IMPORT_LIBRARIES
        ),
    ]


def configure_command(versions, mwcc, relink):
    """configure.py's own arguments, as given (build.ninja reruns it)."""
    all_versions = list(project.ALL_VERSIONS) if not mwcc else [project.version()]
    arguments = [] if versions == all_versions else ["--version", ",".join(v.name for v in versions)]
    arguments += ["--matching"] if relink else ["--mwcc"] if mwcc else []
    return " ".join(["$python configure.py", *arguments])


def generate_clang(versions):
    """build.ninja of the default build: VERSIONS' compilers built with clang."""
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
    outputs = [project.relative(clang.output_path(v)) for v in versions]
    out += [
        f"default {' '.join(outputs)}",
        "build clang: phony " + " ".join(project.relative(clang.output_path(v)) for v in project.ALL_VERSIONS),
        f"build build.ninja: configure | configure.py {LIBRARY} config/sources.json include/version.h "
        + " ".join(f"config/{v}/config.json" for v in project.ALL_VERSIONS),
        "",
    ]
    return "\n".join(out)


def generate(versions, relink):
    """build.ninja of the CodeWarrior workflow (--mwcc): VERSIONS[0] is the one objdiff and --matching take."""
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
        # (no -MD: the normal object, whose depfile tracks the headers, is an input)
        "rule mwcc_debug",
        "  command = " + ENVIRONMENT + "$wibo $compiler_command $cflags $in -o $out",
        "  description = MWCC (debug) $in",
        # TODO: Pro 4 (one MSL source) is built without a depfile: its driver's dependency option is unknown
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
        # (the relink and the source-only compiler take the runtime objects the updater ships: lib/'s download is their
        # rule; the import libraries are the source-only and clang builds')
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
        # (objdiff.json is the selected version's: per function, or with --matching per translation unit)
        out += version_edges(v, objects, objdiff=v == selected and not relink, report=not (relink and v == selected))
        # (the relink of every version with a layout: ninja build/VERSION/relink.ok)
        if (v.config / "splits.txt").exists():
            out += relink_edges(v, objects, debug_objects, selected=relink and v == selected)
        if v == selected:
            selected_objects, selected_debug = objects, debug_objects
    # (each derived version: compiled with its own VERSION and linked from the sources alone)
    for v in project.DERIVED:
        edges, objects, _ = compile_edges(v)
        out += edges + resources_edge(v) + derived_edges(v, objects)
    # (and every version's compiler built with clang: ninja clang)
    for v in project.ALL_VERSIONS:
        out += clang_edges(v)
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
        # (every version built, compared, relinked and its source-only compiler linked: tools/verify.py)
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
    """--version's list (default: every version, or with --mwcc the primary one)."""
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
        description=__doc__.split("\n\n")[0],
        epilog=__doc__.split("\n\n", 1)[1],
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
