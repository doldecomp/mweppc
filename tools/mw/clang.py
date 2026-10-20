"""clang on the sources: the one clang command line for a source (compile_commands.json, diagnostics, syntax trees),
and the compiler built with clang.

A source's clang command is derived from its MWCC compilation (build.compilation), so both see the same include
directories and VERSION. Two views of a source:

- arguments(): as the matching tools read it (diagnostics, the syntax trees the editing tools work on: csrc), MWCC's
  view: a 32-bit Windows clang (i386-pc-windows-msvc, the MS extensions) without MSVC's identity (the SDK and MSL headers
  test _MSC_VER before __MWERKS__), with __MWERKS__ defined as the compiler's own headers expect. Nothing is silenced:
  what clang says of a source is what a modern compiler will.
- build_arguments(): what the clang build compiles it with (configure.py's default build; compile_commands.json, so
  clangd sees what is built). No __MWERKS__: a source's #ifdef __MWERKS__ ... #else holds its clang side.

The clang build (configure.py without --mwcc): every source compiled by clang (build/VERSION/clang/obj), linked by
lld-link with no C library but MSL's into build/VERSION/clang/mwcceppc.exe, with src/imports' import libraries and
src/driver/mwcceppc.rc's resources, as the source-only build is (link_compiler()). Its flags give clang MWCC's semantics:

- MinGW's ABI (i386-pc-windows-gnu) for the compiler: MWCC's enums are as small as their values allow, which only a
  non-Microsoft target gives clang (-fshort-enums), and the compiler's records, written to precompiled headers, must
  lay out as the original's do. Its C is compiled with GNU89's inline rules (an inline function defined in a .c is
  external, as MWCC emits it); the headers' inline functions are static there (common.h's HEADER_INLINE).
- Microsoft's ABI (i386-pc-windows-msvc) for MSL's and the runtime's C: their headers' inline functions are C++'s
  (MWCC compiles MSL under #pragma cplusplus in places), which only the Microsoft ABI gives clang's C, and they share no
  enum with the compiler. include/msl/clang_prefix.h gives them clang's variable arguments.
- no strict aliasing, wrapping signed and pointer arithmetic, null pointer checks kept, tentative definitions COMMON,
  and locals that are read before they are set read as zero: MWCC assumes nothing of undefined behaviour, and clang
  must not either. C++ without exceptions or RTTI (as the compiler's C++ is built).
- floating point in SSE2 (-msse2 -mfpmath=sse): nothing depends on the x87 any more. What MWCC computes on the x87 at
  the precision control's 64 bits (constant folding, the int64 conversions, MSL's decimal conversions) and its fistp
  conversions are include/x87.h's integer arithmetic on the clang side; an x87 build (-mno-sse) compiles the same
  objects.
- one section per function and variable, so that lld-link drops what nothing reaches (/opt:ref) as MWCC's linker does.
- __FILE__ the file's name without its directory, as MWCC's is (-fmacro-prefix-map): the internal-error messages
  print it.

The project's layout is .clang-format's, applied by format_source() to every source a transaction writes.
"""

from __future__ import annotations

import fnmatch
import functools
import json
import os
import re
import shutil
import struct
import subprocess
import sys
from pathlib import Path

from . import build, coff, link
from .project import ROOT, Error, Version, relative, write_atomic
from .project import version as get_version

CASE = ROOT / "build" / "clang" / "case"
COMMANDS = ROOT / "compile_commands.json"
FORMATTED = (".c", ".cpp", ".h")
# (whether the file system the repository is on tells names apart by case: lib/'s headers need lowercase links then)
CASE_SENSITIVE = not (ROOT / "CONFIGURE.PY").exists()

# the clang build: its targets, and the flags that give it MWCC's semantics (above)
MINGW = "i386-pc-windows-gnu"
MICROSOFT = "i386-pc-windows-msvc"
SEMANTICS = (
    "-O2",
    "-fno-strict-aliasing",
    # (wrapping signed and pointer arithmetic: from clang 20, -fwrapv alone leaves pointer overflow undefined)
    "-fno-strict-overflow",
    "-fno-delete-null-pointer-checks",
    "-ftrivial-auto-var-init=zero",
    "-fcommon",
    "-ffunction-sections",
    "-fdata-sections",
    # (SSE2 floating point: x87.h emulates the x87 results MWCC's code gives, so nothing depends on the x87)
    "-msse2",
    "-mfpmath=sse",
)
# (the Macintosh's four-character constants, 'TEXT', are the sources' idiom, and clang gives them MWCC's values)
WARNINGS = ("-Wno-multichar",)
LIBRARY_PREFIX = "include/msl/clang_prefix.h"
# (the directories of the headers whose code names its file with __FILE__: assertions in inline functions)
HEADER_DIRECTORIES = ("include/compiler", "include/driver")
# (the FlexLM stub the compiler loads on Windows: tools/lmgr_stub, as beside every compiler here)
LICENSING = ROOT / "tools/lmgr_stub/lmgr326b.dll"


def _case_shim_path(directory: Path) -> Path:
    return CASE / directory.relative_to(ROOT)


def _case_shim(directory: Path) -> Path | None:
    """The directory of lowercase links to DIRECTORY's files that are not lowercase: lib/'s Win32 SDK headers are
    WINNT.H and the like, included as winnt.h, which only a case-insensitive file system (MWCC's) finds. None where the
    file system is case-insensitive itself (Windows', macOS's by default)."""
    shim = _case_shim_path(directory)
    for path in directory.iterdir():
        if path.is_file() and path.name != path.name.lower():
            lower = directory / path.name.lower()
            if lower.exists() and lower.samefile(path):
                return None
            link_path = shim / path.name.lower()
            # (relative, so that the links survive the repository's moving: the build's stamp keeps them)
            target = Path(os.path.relpath(path, link_path.parent))
            if link_path.is_symlink() and link_path.readlink() != target:
                link_path.unlink()  # (an absolute link of an earlier revision)
            if not link_path.is_symlink():
                link_path.parent.mkdir(parents=True, exist_ok=True)
                link_path.symlink_to(target)
    return shim if shim.is_dir() else None


def _library_directories(compilation: build.Compilation) -> list[Path]:
    return [ROOT / f[2:] for f in compilation.args if f.startswith("-Ilib/")]


def make_case_shims(compilations: list[build.Compilation]) -> None:
    """Make the lowercase links for every lib/ include directory of COMPILATIONS that lib/ has (once it is
    downloaded)."""
    if CASE_SENSITIVE:
        for directory in sorted({d for c in compilations for d in _library_directories(c) if d.is_dir()}):
            _case_shim(directory)


def library(compilation: build.Compilation) -> bool:
    """Whether the source is MSL's or the runtime's C (compiled with Microsoft's ABI by the clang build)."""
    return compilation.source.startswith(("src/msl/", "src/runtime/")) and not _cpp(compilation)


def _cpp(compilation: build.Compilation) -> bool:
    return compilation.source.endswith((".cpp", ".cp"))


def _arguments(compilation: build.Compilation, target: str, mwerks: bool, absolute: bool) -> list[str]:
    flags = compilation.args
    cpp = _cpp(compilation)
    # (MWCC's __option(NAME) is whether an option is on, as the headers' #if tests it: x86's byte order and long long,
    # and in C++ the built-in bool and wchar_t, which clang has whatever -bool says; every other option off)
    options = ["little_endian", "longlong"] + (["cplusplus", "bool", "wchar_type"] if cpp else [])
    if "-Cpp_exceptions" in flags and flags[flags.index("-Cpp_exceptions") + 1] == "on":
        options.append("exceptions")
    args = [
        "clang",
        f"--target={target}",
        "-fms-extensions",
        "-fno-ms-compatibility",
        "-U_MSC_VER",
        "-U_MSC_FULL_VER",
        "-U_MSC_BUILD",
        *(["-D__MWERKS__=0x2407"] if mwerks else []),
        "-D__INTEL__",
        "-D__option(x)=__option_##x",
        *(f"-D__option_{name}=1" for name in options),
        *(["-xc++", "-std=gnu++98"] if cpp else ["-xc", "-std=gnu17"]),
    ]
    # (MWCC's -align N caps structure member alignment at N, 4 by default (a double is 4-aligned): clang's
    # -fpack-struct=N, which #pragma pack(pop) returns to as MWCC's does)
    align = flags[flags.index("-align") + 1] if "-align" in flags else "4"
    args.append("-fpack-struct=" + {"byte": "1", "packed": "1"}.get(align, align))
    system = False
    for flag in flags:
        if flag == "-i-":
            # (MWCC: the include directories after -i- are system ones. The MSL and runtime wrappers include lib/'s
            # file of their own name with <...>, which searches only these)
            system = True
        elif flag.startswith("-I"):
            option = "-isystem" if system else "-I"
            directory = ROOT / flag[2:]
            args.append(option + (str(directory) if absolute else flag[2:]))
            if CASE_SENSITIVE and flag[2:].startswith("lib/"):
                shim = _case_shim_path(directory)
                args.append(option + (str(shim) if absolute else relative(shim)))
        elif flag.startswith("-D"):
            args.append(flag)
    return args


def arguments(compilation: build.Compilation) -> list[str]:
    """clang's arguments for COMPILATION's source as the tools read it, MWCC's view (the program first; without the
    source and -c)."""
    for directory in _library_directories(compilation):
        if CASE_SENSITIVE and directory.is_dir():
            _case_shim(directory)
    return _arguments(compilation, MICROSOFT, mwerks=True, absolute=True)


def _file_names(compilation: build.Compilation) -> list[str]:
    """MWCC's __FILE__ is the file's name alone ('LoopOptimization.c', 'BitVector.h': the internal-error messages
    print it), clang's the path it opened it by: each directory the source or a header of the project's sits in is
    mapped away."""
    directories = {Path(compilation.source).parent.as_posix(), *HEADER_DIRECTORIES}
    return [f"-fmacro-prefix-map={directory}/=" for directory in sorted(directories)]


def build_arguments(compilation: build.Compilation) -> list[str]:
    """The clang build's arguments for COMPILATION's source (the program first; without the source, -c and -o), paths
    relative to the root."""
    lib = library(compilation)
    args = _arguments(compilation, MICROSOFT if lib else MINGW, mwerks=False, absolute=False)
    args += [*SEMANTICS, *WARNINGS, *_file_names(compilation)]
    if lib:
        args += ["-include", LIBRARY_PREFIX]
    else:
        args.append("-fshort-enums")
        args += ["-fno-exceptions", "-fno-rtti"] if _cpp(compilation) else ["-fgnu89-inline"]
    return args


def object_path(version: Version | str | None, source: str) -> Path:
    """The clang build's object of SOURCE."""
    return get_version(version).build / "clang/obj" / f"{source}.obj"


def output_path(version: Version | str | None) -> Path:
    """The clang build's compiler."""
    return get_version(version).build / "clang/mwcceppc.exe"


def write_compile_commands(version: Version | str | None = None) -> int:
    """Write compile_commands.json (for clangd) with VERSION's clang build command for each source; the number of
    sources. (The lowercase links to lib/'s headers are made too, once lib/ is downloaded: ninja makes them for the
    build.)"""
    compilations = build.compilations(version)
    make_case_shims(compilations)
    entries = [
        {
            "directory": str(ROOT),
            "file": str(ROOT / c.source),
            "arguments": [*build_arguments(c), "-c", c.source, "-o", relative(object_path(c.version, c.source))],
        }
        for c in compilations
    ]
    text = json.dumps(entries, indent=1) + "\n"
    if not COMMANDS.exists() or COMMANDS.read_text() != text:
        write_atomic(COMMANDS, text)
    return len(entries)


# ---------------------------------------------------------------------------------------------------- the link

# (section characteristics of the traps: code and data, 16- and 4-aligned, each a COMDAT of its own)
_TRAP_CODE = coff.CODE | coff.EXECUTE | coff.READ | 0x00500000 | 0x1000
_TRAP_DATA = coff.INITIALIZED | coff.READ | coff.WRITE | 0x00300000 | 0x1000
_COMDAT_ANY = 2


def _traps(missing: list[str]) -> coff.Object:
    """An object defining each name of MISSING as a trap (ud2), and each __imp_ name as a pointer to one: each in a
    COMDAT of its own, which lld-link keeps only when something it keeps refers to it."""
    obj = coff.Object()

    def comdat(flags, data, relocations, name, storage, kind):
        section = obj.add_section(".trap", flags, data, relocations=relocations, section_symbol=False)
        aux = struct.pack("<IHHIHBB2x", len(data), len(relocations), 0, 0, 0, _COMDAT_ANY, 0)
        obj.add_symbol(".trap", 0, section.number, 0, coff.STATIC, aux)
        return obj.add_symbol(name, 0, section.number, kind, storage)

    trap = comdat(_TRAP_CODE, b"\x0f\x0b", [], "__clang_build_trap", coff.STATIC, coff.FUNCTION_TYPE)
    for name in missing:
        if name.startswith("__imp_"):
            comdat(_TRAP_DATA, bytes(4), [coff.Relocation(0, trap, coff.DIR32)], name, coff.EXTERNAL, 0)
        else:
            comdat(_TRAP_CODE, b"\x0f\x0b", [], name, coff.EXTERNAL, coff.FUNCTION_TYPE)
    return obj


def _unresolved(objects: list[Path]) -> list[str]:
    """The names OBJECTS refer to and neither they nor src/imports' import libraries define."""
    defined, referenced = set(), set()
    for path in objects:
        try:
            obj = coff.Object.read(path)
        except coff.CoffError as e:
            raise Error(str(e)) from None
        # (MinGW's static constructors and destructors are its runtime's to run, which the clang build does not have:
        # maincrt.c's __main is empty, and _RunInit runs only Microsoft's .CRT$XC* ones)
        if any(s.name.startswith((".ctors", ".dtors")) for s in obj.sections):
            raise Error(f"{relative(path)}: static constructors or destructors, which nothing in the clang build runs")
        for s in obj.symbols:
            if s.storage == coff.EXTERNAL:
                (defined if s.defined or s.common or s.section == -1 else referenced).add(s.name)
        # (a weak external's default is a definition of its own)
        defined |= {s.name for s in obj.symbols if s.storage == 105}
    imports = link.defined_imports()
    defined |= imports | {"__imp_" + name for name in imports}
    return sorted(referenced - defined)


def link_command(inputs: list[str], output: Path, map_path: Path) -> list[str]:
    """lld-link's command line for the clang build."""
    return [
        "lld-link",
        "/nologo",
        "/nodefaultlib",
        "/machine:x86",
        "/subsystem:console",
        "/entry:mainCRTStartup",
        # (what nothing reaches dropped, as MWCC's linker drops it; no identical functions folded: the compiler
        # compares function pointers)
        "/opt:ref,noicf",
        # (the exception handler mainCRTStartup registers is a C function: no table of safe handlers)
        "/safeseh:no",
        "/dynamicbase:no",
        # (the same bytes from the same objects: no time stamp)
        "/Brepro",
        f"/out:{relative(output)}",
        f"/map:{relative(map_path)}",
        *inputs,
    ]


def link_compiler(version: Version | str | None) -> Path:
    """Link VERSION's clang build from its objects (build/VERSION/clang/mwcceppc.exe); the output.

    A name the objects refer to and nothing defines is defined by a trap, as the source-only build traps the code that
    refers to one: such references are in code the original lacks (its linker dropped it), which lld-link drops too.
    A trap the link keeps is an error, naming what refers to it: it would be code that runs."""
    v = get_version(version)
    objects = [object_path(v, c.source) for c in build.compilations(v)]
    absent = [relative(p) for p in objects if not p.exists()]
    if absent:
        raise Error(f"{v}: not compiled: {', '.join(absent[:5])}{' ...' if len(absent) > 5 else ''}")
    output = output_path(v)
    map_path = output.with_suffix(".map")
    missing = _unresolved(objects)
    traps = output.parent / "traps.obj"
    _traps(missing).write(traps)
    inputs = [
        *(relative(p) for p in objects),
        relative(traps),
        *(link.source_import_library(name) for name in link.IMPORT_LIBRARIES),
        f"build/{v}/resources.res",
    ]
    listing = output.parent / "inputs.txt"
    write_atomic(listing, "".join(f"{path}\n" for path in inputs))
    command = link_command([f"@{relative(listing)}"], output, map_path)
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, errors="replace")
    except FileNotFoundError:
        raise Error("lld-link is not installed (LLVM's linker links the clang build)") from None
    if result.returncode:
        output.unlink(missing_ok=True)
        raise Error(f"the link fails:\n{(result.stdout + result.stderr)[-3000:]}")
    # (a trap is in the map only when the link kept it)
    kept = set(
        re.findall(r"^\s*\S+\s+(\S+)\s+[0-9a-f]+\s+(?:\w\s+)*\S*traps\.obj$", map_path.read_text(), re.MULTILINE)
    )
    live = sorted(kept & set(missing))
    if live:
        output.unlink(missing_ok=True)
        raise Error(f"{v}: code that runs refers to what nothing defines: {', '.join(live)}")
    shutil.copy(LICENSING, output.parent / LICENSING.name)
    return output


def run(
    compilation: build.Compilation, extra: list[str], text: str | bytes | None = None, timeout: float = 600
) -> subprocess.CompletedProcess:
    """clang on the source with EXTRA arguments; on TEXT instead of the file when given (read as the source, its
    directory searched for its own includes)."""
    args = arguments(compilation)
    if text is None:
        command = [*args, *extra, compilation.source]
        data = None
    else:
        command = [*args, f"-I{ROOT / Path(compilation.source).parent}", *extra, "-"]
        data = text.encode("latin-1") if isinstance(text, str) else text
    try:
        return subprocess.run(command, cwd=ROOT, input=data, capture_output=True, timeout=timeout)
    except FileNotFoundError:
        raise Error("clang is not installed (the editing tools read the sources with it)") from None


def errors(compilation: build.Compilation, text: str | bytes | None = None) -> list[str]:
    """clang's errors for the source (or TEXT as the source), each "file:line:col: error: ..." with the root's prefix
    removed. Implicit declarations are errors: MWCC takes them too, and they change the code around them."""
    result = run(
        compilation,
        ["-fsyntax-only", "-ferror-limit=0", "-Werror=implicit-function-declaration", "-Werror=implicit-int"],
        text,
    )
    prefix = str(ROOT) + "/"
    return [line.removeprefix(prefix) for line in result.stderr.decode("latin-1").splitlines() if ": error:" in line]


def syntax_tree(compilation: build.Compilation, text: str | bytes | None = None) -> dict:
    """clang's syntax tree of the source (or TEXT as the source): -ast-dump=json's translation unit."""
    result = run(compilation, ["-fsyntax-only", "-ferror-limit=0", "-Xclang", "-ast-dump=json"], text)
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        raise Error(
            f"clang gave no syntax tree for {compilation.source}:\n{result.stderr.decode('latin-1')[-2000:]}"
        ) from None


@functools.cache
def _format_ignored() -> tuple[str, ...]:
    """.clang-format-ignore's patterns (clang-format reads it only for files named on its command line, not stdin)."""
    try:
        lines = (ROOT / ".clang-format-ignore").read_text().splitlines()
    except FileNotFoundError:
        return ()
    return tuple(line.strip().replace("**", "*") for line in lines if line.strip() and not line.startswith("#"))


def formatted(path: Path) -> bool:
    """Whether .clang-format lays out PATH: the project's own C and C++, not lib/ and the library wrappers."""
    try:
        name = path.relative_to(ROOT).as_posix()
    except ValueError:
        return False
    return path.suffix in FORMATTED and not any(fnmatch.fnmatch(name, p) for p in _format_ignored())


def format_source(path: Path, data: bytes) -> bytes:
    """DATA, PATH's new contents, laid out by .clang-format (pyproject.toml's pinned clang-format)."""
    command = [str(Path(sys.executable).parent / "clang-format"), f"--assume-filename={path}"]
    try:
        result = subprocess.run(command, cwd=ROOT, input=data, capture_output=True, timeout=60)
    except FileNotFoundError:
        raise Error("clang-format is not installed (uv sync installs pyproject.toml's pinned version)") from None
    if result.returncode:
        raise Error(f"clang-format failed on {path.relative_to(ROOT)}: {result.stderr.decode(errors='replace')}")
    return result.stdout
