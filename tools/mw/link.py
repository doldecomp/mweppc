from __future__ import annotations

import struct
import subprocess
from dataclasses import dataclass
from pathlib import Path

from . import build, formats
from .coff import DIR32NB, EXTERNAL, STATIC, Object, Relocation
from .pe import PEFile
from .pe import original as original_pe
from .project import ROOT, Error, Version, relative
from .project import version as get_version

MWLD = "build/compilers/pro53/mwld.exe"
LIBRARY_OBJECTS = {"cexc.obj": "lib/runtime/cexc.obj", "ccinit.obj": "lib/runtime/ccinit.obj"}
RUNTIME_OBJECTS = ("lib/runtime/cexc.obj", "lib/runtime/ccinit.obj", "lib/runtime/ctls.obj")
IMPORT_LIBRARIES = ("kernel32", "lmgr326b", "version")


def import_definition(name: str) -> str:
    return f"src/imports/{name}.def"


def source_import_library(name: str) -> str:
    return f"build/imports/{name}.lib"


def defined_imports() -> set[str]:
    out = set()
    for name in IMPORT_LIBRARIES:
        lines = (line.split(";")[0].split() for line in (ROOT / import_definition(name)).read_text().splitlines())
        words = [w for w in lines if w]
        out |= {"_" + w[0] for w in words[words.index(["EXPORTS"]) + 1 :]}
    return out


def split_object(version: Version | str, unit: str) -> str:
    return f"build/{get_version(version)}/obj/{unit}.obj"


def import_libraries(version: Version | str) -> list[str]:
    return [f"build/{get_version(version)}/implib/{name}.lib" for name in IMPORT_LIBRARIES]


def inputs(version: Version | str, units: list[str], compiled: set[str]) -> list[str]:
    v = get_version(version)
    out = [f"build/{v}/obj/sections.obj"]
    for unit in units:
        if unit in LIBRARY_OBJECTS:
            out.append(LIBRARY_OBJECTS[unit])
        elif unit in compiled:
            out.append(relative(build.object_path(v, unit)))
        else:
            out.append(split_object(v, unit))
    return out + import_libraries(v) + [f"build/{v}/obj/resources.res"]


def mwld(inputs: list[str], output: Path | str, *options: str) -> subprocess.CompletedProcess:
    return build.run(
        [
            *build.windows_prefix(),
            MWLD,
            "-nostdlib",
            "-subsystem",
            "console",
            "-m",
            "_mainCRTStartup",
            *inputs,
            "-o",
            str(output),
            *options,
        ]
    )


def errors(result: subprocess.CompletedProcess) -> list[str]:
    return [line.strip("# ").strip() for line in result.stdout.splitlines() if line.startswith("#   ")]


def with_original_stamp(linked: bytes, original: PEFile) -> bytes:
    at = struct.unpack_from("<I", original.data, 0x3C)[0] + 8
    out = bytearray(linked)
    out[at : at + 4] = original.data[at : at + 4]
    return bytes(out)


@dataclass
class Import:
    dll: str
    symbol: str
    number: int
    by_name: bool
    slot: int


def imports(version: Version | str) -> list[Import]:
    v = get_version(version)
    named = {s.address: s.name for s in formats.read_symbols(v.config / "symbols.txt") if s.section == ".idata"}
    out = []
    for item in original_pe(v).imports:
        if item.slot not in named:
            raise Error(f"config/{v}/symbols.txt names no import at 0x{item.slot:08x} ({item.dll})")
        out.append(
            Import(item.dll, named[item.slot].removeprefix("__imp_"), item.number, item.name is not None, item.slot)
        )
    return out


def import_library(dll: str, entries: list[Import], code: bool = False) -> bytes:
    base = dll.rsplit(".", 1)[0]
    descriptor, null_descriptor, null_thunk = (
        f"__IMPORT_DESCRIPTOR_{base}",
        "__NULL_IMPORT_DESCRIPTOR",
        f"\x7f{base}_NULL_THUNK_DATA",
    )
    name = dll.encode() + b"\0"
    name += b"\0" * (len(name) & 1)

    def member(sections, symbols):
        obj = Object()
        for section_name, flags, data, relocations in sections:
            obj.add_section(section_name, flags, data, relocations=relocations, section_symbol=False)
        for symbol_name, value, number, storage in symbols:
            obj.add_symbol(symbol_name, value, number, 0, storage)
        return obj.to_bytes()

    section_class = 104
    members = [
        (
            member(
                [
                    (
                        ".idata$2",
                        0xC0300040,
                        bytes(20),
                        [Relocation(12, 2, DIR32NB), Relocation(0, 3, DIR32NB), Relocation(16, 4, DIR32NB)],
                    ),
                    (".idata$6", 0xC0200040, name, []),
                ],
                [
                    (descriptor, 0, 1, EXTERNAL),
                    (".idata$2", 0xC0000040, 1, section_class),
                    (".idata$6", 0, 2, STATIC),
                    (".idata$4", 0xC0000040, 0, section_class),
                    (".idata$5", 0xC0000040, 0, section_class),
                    (null_descriptor, 0, 0, EXTERNAL),
                    (null_thunk, 0, 0, EXTERNAL),
                ],
            ),
            [descriptor],
        ),
        (member([(".idata$3", 0xC0300040, bytes(20), [])], [(null_descriptor, 0, 1, EXTERNAL)]), [null_descriptor]),
        (
            member(
                [(".idata$5", 0xC0300040, bytes(4), []), (".idata$4", 0xC0300040, bytes(4), [])],
                [(null_thunk, 0, 1, EXTERNAL)],
            ),
            [null_thunk],
        ),
    ]
    for entry in entries:
        strings = entry.symbol.encode() + b"\0" + dll.encode() + b"\0"
        kind = (0 if code else 1) | (3 if entry.by_name else 0) << 2
        header = struct.pack("<HHHHIIHH", 0, 0xFFFF, 0, 0x14C, 0, len(strings), entry.number, kind)
        members.append((header + strings, [f"__imp_{entry.symbol}"] + ([entry.symbol] if code else [])))

    symbol_members = [(symbol, i) for i, (_, names) in enumerate(members) for symbol in names]
    first_size = 4 + 4 * len(symbol_members) + sum(len(s) + 1 for s, _ in symbol_members)
    second_size = 4 + 4 * len(members) + 4 + 2 * len(symbol_members) + sum(len(s) + 1 for s, _ in symbol_members)
    offset = 8 + 60 + first_size + (first_size & 1) + 60 + second_size + (second_size & 1)
    offsets = []
    for data, _ in members:
        offsets.append(offset)
        offset += 60 + len(data) + (len(data) & 1)

    def header(member_name, size):
        return f"{member_name:<16}{0:<12}{'':<6}{'':<6}{'0':<8}{size:<10}`\n".encode()

    first = struct.pack(
        f">I{len(symbol_members)}I", len(symbol_members), *(offsets[i] for _, i in symbol_members)
    ) + b"".join(s.encode("latin-1") + b"\0" for s, _ in symbol_members)
    ordered = sorted(symbol_members)
    second = (
        struct.pack(f"<I{len(members)}II", len(members), *offsets, len(ordered))
        + struct.pack(f"<{len(ordered)}H", *(i + 1 for _, i in ordered))
        + b"".join(s.encode("latin-1") + b"\0" for s, _ in ordered)
    )
    out = bytearray(b"!<arch>\n")
    for member_name, data in [("/", first), ("/", second)] + [(dll + "/", data) for data, _ in members]:
        out += header(member_name, len(data)) + data + b"\n" * (len(data) & 1)
    return bytes(out)


def import_libraries_of(version: Version | str, code: bool = False) -> dict[str, bytes]:
    by_dll: dict[str, list[Import]] = {}
    for item in imports(version):
        by_dll.setdefault(item.dll, []).append(item)
    return {
        dll.rsplit(".", 1)[0].lower() + ".lib": import_library(dll, entries, code) for dll, entries in by_dll.items()
    }
