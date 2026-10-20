"""PE32 executables: the original compiler (read by virtual address), its imports and base relocations.

original(version) is the one way to get a version's original executable: it checks the file's SHA-1 against the
version's config.json, so nothing ever works from a different or damaged binary.
"""

from __future__ import annotations

import functools
import hashlib
import struct
from dataclasses import dataclass
from pathlib import Path

from .project import Error, Version, relative
from .project import version as get_version

CODE = 0x20
INITIALIZED = 0x40
UNINITIALIZED = 0x80
EXECUTE = 0x20000000

# data directory indices
EXPORTS, IMPORTS, RESOURCES, BASE_RELOCATIONS, TLS = 0, 1, 2, 5, 9


@dataclass(frozen=True)
class Section:
    name: str
    virtual_address: int  # absolute (image base included)
    virtual_size: int
    file_offset: int
    file_size: int
    characteristics: int

    @property
    def end(self) -> int:
        return self.virtual_address + max(self.virtual_size, self.file_size)

    @property
    def code(self) -> bool:
        return bool(self.characteristics & EXECUTE)

    @property
    def bss(self) -> bool:
        """Uninitialized data with no bytes in the file."""
        return bool(self.characteristics & UNINITIALIZED) and self.file_size == 0


@dataclass(frozen=True)
class Import:
    dll: str
    name: str | None  # None when imported by ordinal
    number: int  # the ordinal, or the name's hint
    slot: int  # the address of its import address table entry


class PEFile:
    def __init__(self, path: Path | str):
        self.path = Path(path)
        self.data = self.path.read_bytes()
        if self.data[:2] != b"MZ":
            raise Error(f"{self.path} is not an executable")
        self.coff = struct.unpack_from("<I", self.data, 0x3C)[0] + 4
        if self.data[self.coff - 4 : self.coff] != b"PE\0\0":
            raise Error(f"{self.path} is not a PE executable")
        _, count, self.timestamp = struct.unpack_from("<HHI", self.data, self.coff)
        optional_size = struct.unpack_from("<H", self.data, self.coff + 16)[0]
        self.optional = self.coff + 20
        if struct.unpack_from("<H", self.data, self.optional)[0] != 0x10B:
            raise Error(f"{self.path} is not a 32-bit PE executable")
        self.image_base = struct.unpack_from("<I", self.data, self.optional + 28)[0]
        self.checksum = struct.unpack_from("<I", self.data, self.optional + 64)[0]
        table = self.optional + optional_size
        sections = []
        for i in range(count):
            at = table + i * 40
            virtual_size, rva, file_size, file_offset = struct.unpack_from("<IIII", self.data, at + 8)
            sections.append(
                Section(
                    self.data[at : at + 8].split(b"\0", 1)[0].decode("ascii"),
                    self.image_base + rva,
                    virtual_size,
                    file_offset,
                    file_size,
                    struct.unpack_from("<I", self.data, at + 36)[0],
                )
            )
        self.sections = tuple(sections)
        self._found: dict = {}

    # ------------------------------------------------------------------ addresses

    def section_for_address(self, address: int) -> Section:
        for section in self.sections:
            if section.virtual_address <= address < section.end:
                return section
        raise ValueError(f"address 0x{address:x} is not in a section")

    def address_to_offset(self, address: int) -> int:
        section = self.section_for_address(address)
        if address - section.virtual_address >= section.file_size:
            raise ValueError(f"address 0x{address:x} has no file-backed data")
        return section.file_offset + address - section.virtual_address

    def read(self, address: int, size: int) -> bytes:
        """SIZE bytes at ADDRESS (fewer at the end of a section's file data)."""
        offset = self.address_to_offset(address)
        return self.data[offset : offset + size]

    def word(self, address: int) -> int:
        return struct.unpack("<I", self.read(address, 4))[0]

    def find(self, needle: bytes, limit: int | None = None) -> list[int]:
        """The addresses NEEDLE occurs at in the data (non-code) sections, the first LIMIT of them."""
        key = (needle, limit)
        if key not in self._found:
            addresses = []
            for section in self.sections:
                if section.code:
                    continue
                data = self.data[section.file_offset : section.file_offset + section.file_size]
                start = 0
                while (limit is None or len(addresses) < limit) and (at := data.find(needle, start)) >= 0:
                    addresses.append(section.virtual_address + at)
                    start = at + 1
            self._found[key] = addresses
        return self._found[key]

    # ------------------------------------------------------------------ directories

    def directory(self, index: int) -> tuple[int, int]:
        """(absolute address, size) of data directory INDEX; (0, 0) when absent."""
        rva, size = struct.unpack_from("<II", self.data, self.optional + 96 + 8 * index)
        return (self.image_base + rva if rva else 0), size

    def string(self, address: int, limit: int = 256) -> str:
        return self.read(address, limit).split(b"\0", 1)[0].decode("latin-1")

    @functools.cached_property
    def imports(self) -> tuple[Import, ...]:
        """Every import, in the import directory's order."""
        address, _ = self.directory(IMPORTS)
        out = []
        while address:
            lookup, _, _, name, first = struct.unpack_from("<IIIII", self.read(address, 20))
            if not name:
                break
            dll = self.string(self.image_base + name)
            thunk = self.image_base + (lookup or first)
            slot = self.image_base + first
            while entry := self.word(thunk):
                if entry & 0x80000000:
                    out.append(Import(dll, None, entry & 0xFFFF, slot))
                else:
                    hint = struct.unpack_from("<H", self.read(self.image_base + entry, 2))[0]
                    out.append(Import(dll, self.string(self.image_base + entry + 2), hint, slot))
                thunk += 4
                slot += 4
            address += 20
        return tuple(out)

    @functools.cached_property
    def base_relocations(self) -> tuple[int, ...]:
        """The addresses of the absolute (HIGHLOW) fixups of the base relocation table, sorted."""
        address, size = self.directory(BASE_RELOCATIONS)
        offset, fixups = self.address_to_offset(address), []
        end = offset + size
        while offset < end:
            page, block = struct.unpack_from("<II", self.data, offset)
            if block < 8:
                raise Error(f"{self.path.name}: a base relocation block of {block} bytes")
            for i in range((block - 8) // 2):
                entry = struct.unpack_from("<H", self.data, offset + 8 + 2 * i)[0]
                if entry >> 12 == 3:
                    fixups.append(self.image_base + page + (entry & 0xFFF))
                elif entry >> 12 != 0:
                    raise Error(f"{self.path.name}: base relocation type {entry >> 12} is not handled")
            offset += block
        return tuple(sorted(fixups))


@functools.cache
def original(version: Version | str | None = None) -> PEFile:
    """VERSION's original executable, checked against the SHA-1 its config.json gives."""
    from .config import version_config

    v = get_version(version, original=True)
    cfg = version_config(v)
    path = cfg.original
    if not path.exists():
        raise Error(f"{relative(path)} does not exist: ninja downloads the original compilers")
    data = path.read_bytes()
    if hashlib.sha1(data).hexdigest() != cfg.sha1:
        raise Error(f"{relative(path)}: SHA-1 is not {cfg.sha1} (config/{v}/config.json)")
    return PEFile(path)
