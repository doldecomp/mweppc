from __future__ import annotations

import bisect
import functools
import struct
from dataclasses import dataclass
from pathlib import Path

from .names import c_name
from .pe import PEFile
from .project import ROOT, Error, relative

DEFAULT = ROOT / "build/compilers/Wii/1.0RC1/mwcceppc.exe"

_DEBUG_DIRECTORY = 6
_SST_MODULE, _SST_ALIGN_SYM, _SST_GLOBAL_PUB = 0x120, 0x125, 0x12A
_S_PUB32, _S_LPROC32, _S_GPROC32 = 0x1009, 0x100A, 0x100B


@dataclass(frozen=True)
class Contribution:
    module: str
    section: str
    address: int
    size: int


@dataclass(frozen=True)
class Public:
    name: str
    address: int
    section: str
    module: str

    @property
    def code(self) -> bool:
        return self.section == ".text"


class Image:
    def __init__(self, path: Path | str):
        self.pe = PEFile(path)
        self.contributions: list[Contribution] = []
        self.publics: list[Public] = []
        self.procedures: dict[int, str] = {}
        self._read()

    def _read(self) -> None:
        pe, d = self.pe, self.pe.data
        directory, _ = pe.directory(_DEBUG_DIRECTORY)
        if not directory:
            raise Error(f"{relative(pe.path)} has no debug directory")
        base = struct.unpack_from("<I", pe.read(directory, 28), 24)[0]
        if d[base : base + 4] != b"NB11":
            raise Error(f"{relative(pe.path)}: its debug information is not CodeView NB11")
        header = base + struct.unpack_from("<I", d, base + 4)[0]
        header_size, entry_size, count = struct.unpack_from("<HHI", d, header)
        entries = [struct.unpack_from("<HHII", d, header + header_size + entry_size * i) for i in range(count)]

        def pstring(at):
            return d[at + 1 : at + 1 + d[at]].decode("latin-1")

        def address(segment, offset):
            return pe.sections[segment - 1].virtual_address + offset

        for kind, _, offset, _ in entries:
            if kind == _SST_MODULE:
                at = base + offset
                segments = struct.unpack_from("<H", d, at + 4)[0]
                module = pstring(at + 8 + 12 * segments).removesuffix(".obj")
                for i in range(segments):
                    segment, _, start, size = struct.unpack_from("<HHII", d, at + 8 + 12 * i)
                    self.contributions.append(
                        Contribution(module, pe.sections[segment - 1].name, address(segment, start), size)
                    )
        self.contributions.sort(key=lambda c: c.address)
        self._starts = [c.address for c in self.contributions]
        module_of = self.module_at

        def walk(at, end):
            while at < end:
                length, kind = struct.unpack_from("<HH", d, at)
                if not length:
                    break
                if kind == _S_PUB32:
                    _, offset, segment = struct.unpack_from("<IIH", d, at + 4)
                    where = address(segment, offset)
                    self.publics.append(
                        Public(pstring(at + 14), where, pe.sections[segment - 1].name, module_of(where))
                    )
                elif kind in (_S_LPROC32, _S_GPROC32):
                    offset, segment = struct.unpack_from("<IH", d, at + 32)
                    self.procedures.setdefault(address(segment, offset), pstring(at + 39))
                at += 2 + length

        for kind, _, offset, size in entries:
            at = base + offset
            if kind == _SST_GLOBAL_PUB:
                walk(at + 16, at + 16 + struct.unpack_from("<I", d, at + 4)[0])
            elif kind == _SST_ALIGN_SYM:
                walk(at + 4, at + size)
        self.publics.sort(key=lambda p: p.address)

    def module_at(self, address: int) -> str:
        i = bisect.bisect_right(self._starts, address) - 1
        if i >= 0 and address < self.contributions[i].address + self.contributions[i].size:
            return self.contributions[i].module
        return ""

    def names(self) -> dict[int, str]:
        out = {address: c_name(name) for address, name in self.procedures.items()}
        for p in self.publics:
            if p.code:
                out.setdefault(p.address, c_name(p.name))
        return out

    def functions(self) -> list[Contribution]:
        return [c for c in self.contributions if c.section == ".text"]


@functools.cache
def read(path: Path | str = DEFAULT) -> Image:
    path = Path(path)
    if not path.exists():
        raise Error(f"{relative(path)} does not exist: ninja downloads the compilers")
    return Image(path)
