from __future__ import annotations

import dataclasses
import fnmatch
import functools
import json
import os
import platform
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
CASE_SENSITIVE = not (ROOT / "CONFIGURE.PY").exists()

MINGW = "i386-pc-windows-gnu"
MICROSOFT = "i386-pc-windows-msvc"
SEMANTICS = (
    "-O2",
    "-fno-strict-aliasing",
    "-fno-strict-overflow",
    "-fno-delete-null-pointer-checks",
    "-ftrivial-auto-var-init=zero",
    "-fcommon",
    "-ffunction-sections",
    "-fdata-sections",
    "-msse2",
    "-mfpmath=sse",
)
WARNINGS = ("-Wno-multichar",)
LIBRARY_PREFIX = "include/msl/clang_prefix.h"
HEADER_DIRECTORIES = ("include/compiler", "include/driver")
LICENSING = ROOT / "tools/lmgr_stub/lmgr326b.dll"
HOST_SOURCES = ("src/host/ShiftJIS.c",)
HOST_LIKE = "src/driver/MsDos.c"


def _case_shim_path(directory: Path) -> Path:
    return CASE / directory.relative_to(ROOT)


def _case_shim(directory: Path) -> Path | None:
    shim = _case_shim_path(directory)
    for path in directory.iterdir():
        if path.is_file() and path.name != path.name.lower():
            lower = directory / path.name.lower()
            if lower.exists() and lower.samefile(path):
                return None
            link_path = shim / path.name.lower()
            target = Path(os.path.relpath(path, link_path.parent))
            if link_path.is_symlink() and link_path.readlink() != target:
                link_path.unlink()
            if not link_path.is_symlink():
                link_path.parent.mkdir(parents=True, exist_ok=True)
                link_path.symlink_to(target)
    return shim if shim.is_dir() else None


def _library_directories(compilation: build.Compilation) -> list[Path]:
    return [ROOT / f[2:] for f in compilation.args if f.startswith("-Ilib/")]


def make_case_shims(compilations: list[build.Compilation]) -> None:
    if CASE_SENSITIVE:
        for directory in sorted({d for c in compilations for d in _library_directories(c) if d.is_dir()}):
            _case_shim(directory)


def library(compilation: build.Compilation) -> bool:
    return compilation.source.startswith(("src/msl/", "src/runtime/")) and not _cpp(compilation)


def _cpp(compilation: build.Compilation) -> bool:
    return compilation.source.endswith((".cpp", ".cp"))


def _arguments(compilation: build.Compilation, target: str, mwerks: bool, absolute: bool) -> list[str]:
    flags = compilation.args
    cpp = _cpp(compilation)
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
    align = flags[flags.index("-align") + 1] if "-align" in flags else "4"
    args.append("-fpack-struct=" + {"byte": "1", "packed": "1"}.get(align, align))
    system = False
    for flag in flags:
        if flag == "-i-":
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
    for directory in _library_directories(compilation):
        if CASE_SENSITIVE and directory.is_dir():
            _case_shim(directory)
    return _arguments(compilation, MICROSOFT, mwerks=True, absolute=True)


def _file_names(compilation: build.Compilation) -> list[str]:
    directories = {Path(compilation.source).parent.as_posix(), *HEADER_DIRECTORIES}
    return [f"-fmacro-prefix-map={directory}/=" for directory in sorted(directories)]


def build_arguments(compilation: build.Compilation) -> list[str]:
    lib = library(compilation)
    args = _arguments(compilation, MICROSOFT if lib else MINGW, mwerks=False, absolute=False)
    args += [*SEMANTICS, *WARNINGS, *_file_names(compilation)]
    if lib:
        args += ["-include", LIBRARY_PREFIX]
    else:
        args.append("-fshort-enums")
        args += ["-fno-exceptions", "-fno-rtti"] if _cpp(compilation) else ["-fgnu89-inline"]
    return args


def compilations(version: Version | str | None = None) -> list[build.Compilation]:
    sources = build.compilations(version)
    like = next(c for c in sources if c.source == HOST_LIKE)
    return sources + [dataclasses.replace(like, source=source) for source in HOST_SOURCES]


def object_path(version: Version | str | None, source: str) -> Path:
    return get_version(version).build / "clang/obj" / f"{source}.obj"


def output_path(version: Version | str | None) -> Path:
    return get_version(version).build / "clang/mwcceppc.exe"


LINUX = "i386-unknown-linux-gnu"
LINUX_DIRECTORY = "src/host/linux/"
LINUX_SOURCES = (
    "src/host/linux/Start.c",
    "src/host/linux/Kernel32.c",
    "src/host/linux/Heap.c",
    "src/host/linux/License.c",
)
LINUX_EXCLUDED = ("src/runtime/maincrt.c",)
LINUX_FLAGS = (
    "-fno-pic",
    "-fno-pie",
    "-fno-stack-protector",
    "-fno-builtin-bcmp",
    "-fno-builtin-stpcpy",
    "-mms-bitfields",
    "-nostdlibinc",
    "-Ddllimport=",
    "-DMWCC_LINUX=1",
)
LINUX_MICROSOFT = ("-mlong-double-64", "-D_INTEGRAL_MAX_BITS=64")


def native_supported() -> bool:
    return sys.platform.startswith("linux") and platform.machine().lower() in ("x86_64", "amd64", "i386", "i686")


def native_compilations(version: Version | str | None = None) -> list[build.Compilation]:
    sources = [c for c in compilations(version) if c.source not in LINUX_EXCLUDED]
    like = next(c for c in sources if c.source == "src/runtime/setupargs.c")
    return sources + [dataclasses.replace(like, source=source) for source in LINUX_SOURCES]


def native_library(compilation: build.Compilation) -> bool:
    return library(compilation) or compilation.source.startswith(LINUX_DIRECTORY)


def native_arguments(compilation: build.Compilation) -> list[str]:
    lib = native_library(compilation)
    args = _arguments(compilation, LINUX, mwerks=False, absolute=False)
    args += [*SEMANTICS, *WARNINGS, *_file_names(compilation), *LINUX_FLAGS]
    if lib:
        args += [*LINUX_MICROSOFT, "-include", LIBRARY_PREFIX]
    else:
        args.append("-fshort-enums")
        args += ["-fno-exceptions", "-fno-rtti"] if _cpp(compilation) else ["-fgnu89-inline"]
    if compilation.source == "src/host/linux/Start.c":
        args.append(f'-DNATIVE_RESOURCES="{native_resources(compilation.version)}"')
    return args


def native_resources(version: Version | str | None) -> str:
    return f"build/{get_version(version)}/resources.res"


def native_object_path(version: Version | str | None, source: str) -> Path:
    return get_version(version).build / "native/obj" / f"{source}.o"


def native_output_path(version: Version | str | None) -> Path:
    return get_version(version).build / "native/mwcceppc"


def native_link_command(inputs: list[str], output: Path, map_path: Path) -> list[str]:
    return [
        "ld.lld",
        "-m",
        "elf_i386",
        "-static",
        "-nostdlib",
        "--entry=_start",
        "--gc-sections",
        "-o",
        relative(output),
        f"-Map={relative(map_path)}",
        *inputs,
    ]


def link_native(version: Version | str | None) -> Path:
    v = get_version(version)
    objects = [native_object_path(v, c.source) for c in native_compilations(v)]
    absent = [relative(p) for p in objects if not p.exists()]
    if absent:
        raise Error(f"{v}: not compiled: {', '.join(absent[:5])}{' ...' if len(absent) > 5 else ''}")
    output = native_output_path(v)
    listing = output.parent / "inputs.txt"
    write_atomic(listing, "".join(f"{relative(p)}\n" for p in objects))
    command = native_link_command([f"@{relative(listing)}"], output, output.with_suffix(".map"))
    try:
        result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True, errors="replace")
    except FileNotFoundError:
        raise Error("ld.lld is not installed (LLVM's linker links the Linux build)") from None
    if result.returncode or result.stdout.strip() or result.stderr.strip():
        output.unlink(missing_ok=True)
        raise Error(f"the link fails:\n{(result.stdout + result.stderr)[-3000:]}")
    if ".init_array" in output.with_suffix(".map").read_text():
        output.unlink(missing_ok=True)
        raise Error(f"{v}: static constructors, which nothing in the Linux build runs")
    return output


def write_compile_commands(version: Version | str | None = None) -> int:
    sources = compilations(version)
    make_case_shims(sources)
    entries = [
        {
            "directory": str(ROOT),
            "file": str(ROOT / c.source),
            "arguments": [*build_arguments(c), "-c", c.source, "-o", relative(object_path(c.version, c.source))],
        }
        for c in sources
    ] + [
        {
            "directory": str(ROOT),
            "file": str(ROOT / c.source),
            "arguments": [
                *native_arguments(c),
                "-c",
                c.source,
                "-o",
                relative(native_object_path(c.version, c.source)),
            ],
        }
        for c in native_compilations(version)
        if c.source in LINUX_SOURCES
    ]
    text = json.dumps(entries, indent=1) + "\n"
    if not COMMANDS.exists() or COMMANDS.read_text() != text:
        write_atomic(COMMANDS, text)
    return len(entries)


_TRAP_CODE = coff.CODE | coff.EXECUTE | coff.READ | 0x00500000 | 0x1000
_TRAP_DATA = coff.INITIALIZED | coff.READ | coff.WRITE | 0x00300000 | 0x1000
_COMDAT_ANY = 2


def _traps(missing: list[str]) -> coff.Object:
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
    defined, referenced = set(), set()
    for path in objects:
        try:
            obj = coff.Object.read(path)
        except coff.CoffError as e:
            raise Error(str(e)) from None
        if any(s.name.startswith((".ctors", ".dtors")) for s in obj.sections):
            raise Error(f"{relative(path)}: static constructors or destructors, which nothing in the clang build runs")
        for s in obj.symbols:
            if s.storage == coff.EXTERNAL:
                (defined if s.defined or s.common or s.section == -1 else referenced).add(s.name)
        defined |= {s.name for s in obj.symbols if s.storage == 105}
    imports = link.defined_imports()
    defined |= imports | {"__imp_" + name for name in imports}
    return sorted(referenced - defined)


def link_command(inputs: list[str], output: Path, map_path: Path) -> list[str]:
    return [
        "lld-link",
        "/nologo",
        "/nodefaultlib",
        "/machine:x86",
        "/subsystem:console",
        "/entry:mainCRTStartup",
        "/opt:ref,noicf",
        "/safeseh:no",
        "/dynamicbase:no",
        "/Brepro",
        f"/out:{relative(output)}",
        f"/map:{relative(map_path)}",
        *inputs,
    ]


def link_compiler(version: Version | str | None) -> Path:
    v = get_version(version)
    objects = [object_path(v, c.source) for c in compilations(v)]
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
    result = run(
        compilation,
        ["-fsyntax-only", "-ferror-limit=0", "-Werror=implicit-function-declaration", "-Werror=implicit-int"],
        text,
    )
    prefix = str(ROOT) + "/"
    return [line.removeprefix(prefix) for line in result.stderr.decode("latin-1").splitlines() if ": error:" in line]


def syntax_tree(compilation: build.Compilation, text: str | bytes | None = None) -> dict:
    result = run(compilation, ["-fsyntax-only", "-ferror-limit=0", "-Xclang", "-ast-dump=json"], text)
    try:
        return json.loads(result.stdout)
    except json.JSONDecodeError:
        raise Error(
            f"clang gave no syntax tree for {compilation.source}:\n{result.stderr.decode('latin-1')[-2000:]}"
        ) from None


@functools.cache
def _format_ignored() -> tuple[str, ...]:
    try:
        lines = (ROOT / ".clang-format-ignore").read_text().splitlines()
    except FileNotFoundError:
        return ()
    return tuple(line.strip().replace("**", "*") for line in lines if line.strip() and not line.startswith("#"))


def formatted(path: Path) -> bool:
    try:
        name = path.relative_to(ROOT).as_posix()
    except ValueError:
        return False
    return path.suffix in FORMATTED and not any(fnmatch.fnmatch(name, p) for p in _format_ignored())


def format_source(path: Path, data: bytes) -> bytes:
    command = [str(Path(sys.executable).parent / "clang-format"), f"--assume-filename={path}"]
    try:
        result = subprocess.run(command, cwd=ROOT, input=data, capture_output=True, timeout=60)
    except FileNotFoundError:
        raise Error("clang-format is not installed (uv sync installs pyproject.toml's pinned version)") from None
    if result.returncode:
        raise Error(f"clang-format failed on {path.relative_to(ROOT)}: {result.stderr.decode(errors='replace')}")
    return result.stdout
