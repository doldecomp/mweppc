"""Relocatable i386 COFF objects: read the compiler's, write the tools' own.

An Object holds sections (data and relocations) and the symbol table with its indices as the file numbers them
(auxiliary records take indices of their own, so a symbol's index is stable between reading and writing). Names are
latin-1, as the CodeWarrior compilers write them.

Writing lays an object out the way every tool here always has: the file header, the section headers, then each
section's data followed by its relocations, then the symbol table and the string table. Reading an object written so
and writing it again gives the same bytes; the compiler's own objects (whose raw-data and line-number pointers differ)
come out equivalent, not identical.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass, field
from pathlib import Path

from .project import write_atomic

I386 = 0x14C

# section characteristics
CODE = 0x20
INITIALIZED = 0x40
UNINITIALIZED = 0x80
EXECUTE = 0x20000000
READ = 0x40000000
WRITE = 0x80000000

# symbol storage classes
EXTERNAL = 2
STATIC = 3
FUNCTION_TYPE = 0x20

# relocation kinds (IMAGE_REL_I386_*)
DIR32 = 6
DIR32NB = 7
REL32 = 20


class CoffError(ValueError):
    pass


@dataclass
class Relocation:
    offset: int  # in the section
    symbol: int  # symbol index
    kind: int


@dataclass
class Section:
    name: str
    flags: int
    data: bytes  # b"" for an uninitialized section (its size is in .size)
    size: int
    relocations: list[Relocation] = field(default_factory=list)
    number: int = 0  # 1-based, as symbols refer to it

    @property
    def code(self) -> bool:
        return bool(self.flags & CODE)

    @property
    def bss(self) -> bool:
        return bool(self.flags & UNINITIALIZED) and not self.flags & (CODE | INITIALIZED)

    @property
    def alignment(self) -> int:
        """The section's alignment (IMAGE_SCN_ALIGN_*), 1 if unset."""
        code = (self.flags >> 20) & 0xF
        return 1 << (code - 1) if code else 1


@dataclass
class Symbol:
    index: int
    name: str
    value: int
    section: int  # 1-based section number; 0 undefined (or COMMON with a value), -1 absolute, -2 debug
    type: int
    storage: int
    aux: bytes = b""  # the auxiliary records, raw (18 bytes each)

    @property
    def defined(self) -> bool:
        return self.section > 0

    @property
    def external(self) -> bool:
        return self.storage == EXTERNAL

    @property
    def common(self) -> bool:
        return self.storage == EXTERNAL and self.section == 0 and self.value > 0

    @property
    def undefined(self) -> bool:
        return self.storage == EXTERNAL and self.section == 0 and self.value == 0

    @property
    def function(self) -> bool:
        return bool(self.type & FUNCTION_TYPE)

    @property
    def records(self) -> int:
        return 1 + len(self.aux) // 18


class Object:
    def __init__(self):
        self.sections: list[Section] = []
        self.symbols: list[Symbol] = []  # in table order
        self.by_index: dict[int, Symbol] = {}
        self.timestamp = 0
        self._next_index = 0
        self._externals: dict[str, int] = {}

    # ---------------------------------------------------------------- reading

    @classmethod
    def read(cls, source: Path | str | bytes) -> Object:
        data = source if isinstance(source, bytes) else Path(source).read_bytes()
        where = "object" if isinstance(source, bytes) else str(source)
        if len(data) < 20:
            raise CoffError(f"{where}: too short for a COFF header")
        machine, count, timestamp, symbol_offset, symbol_count, optional, _ = struct.unpack_from("<HHIIIHH", data)
        if machine != I386 or optional:
            raise CoffError(f"{where}: not a relocatable i386 COFF object")
        strings_at = symbol_offset + symbol_count * 18
        strings = data[strings_at:]

        def string(offset: int) -> str:
            end = strings.find(b"\0", offset)
            if end < 0:
                raise CoffError(f"{where}: string table entry {offset} is not terminated")
            return strings[offset:end].decode("latin-1")

        obj = cls()
        obj.timestamp = timestamp
        for i in range(count):
            at = 20 + i * 40
            raw_name = data[at : at + 8]
            size, data_at, relocations_at, _, relocation_count, _, flags = struct.unpack_from("<IIIIHHI", data, at + 16)
            name = raw_name.rstrip(b"\0").decode("latin-1")
            if name.startswith("/") and name[1:].isdigit():
                name = string(int(name[1:]))
            relocations = [
                Relocation(*struct.unpack_from("<IIH", data, relocations_at + j * 10)) for j in range(relocation_count)
            ]
            bss = bool(flags & UNINITIALIZED) and not flags & (CODE | INITIALIZED)
            body = b"" if bss or not data_at else data[data_at : data_at + size]
            obj.sections.append(Section(name, flags, body, size, relocations, i + 1))
        i = 0
        while i < symbol_count:
            at = symbol_offset + i * 18
            raw_name = data[at : at + 8]
            value, section, kind, storage, aux_count = struct.unpack_from("<IhHBB", data, at + 8)
            name = (
                string(struct.unpack_from("<I", raw_name, 4)[0])
                if raw_name[:4] == b"\0\0\0\0"
                else raw_name.rstrip(b"\0").decode("latin-1")
            )
            aux = data[at + 18 : at + 18 + aux_count * 18]
            symbol = Symbol(i, name, value, section, kind, storage, aux)
            obj.symbols.append(symbol)
            obj.by_index[i] = symbol
            i += 1 + aux_count
        obj._next_index = i
        return obj

    # ---------------------------------------------------------------- queries

    def symbol(self, index: int) -> Symbol:
        """The symbol at INDEX (as a relocation names it)."""
        try:
            return self.by_index[index]
        except KeyError:
            raise CoffError(f"no symbol at index {index}") from None

    def named(self, name: str) -> list[Symbol]:
        return [s for s in self.symbols if s.name == name]

    def section(self, number: int) -> Section:
        if not 1 <= number <= len(self.sections):
            raise CoffError(f"no section {number}")
        return self.sections[number - 1]

    # ---------------------------------------------------------------- building

    def add_section(
        self,
        name: str,
        flags: int,
        data: bytes = b"",
        size: int | None = None,
        relocations: list[Relocation] | None = None,
        section_symbol: bool = True,
    ) -> Section:
        """A new section (with its section symbol and auxiliary record, as compilers write them)."""
        section = Section(
            name, flags, data, len(data) if size is None else size, relocations or [], len(self.sections) + 1
        )
        self.sections.append(section)
        if section_symbol:
            self.add_symbol(
                name,
                0,
                section.number,
                0,
                STATIC,
                struct.pack("<IHHIHBB2x", section.size, len(section.relocations), 0, 0, 0, 0, 0),
            )
        return section

    def add_symbol(self, name: str, value: int, section: int, kind: int, storage: int, aux: bytes = b"") -> int:
        if len(aux) % 18:
            raise CoffError("auxiliary records are 18 bytes each")
        index = self._next_index
        symbol = Symbol(index, name, value, section, kind, storage, aux)
        self.symbols.append(symbol)
        self.by_index[index] = symbol
        self._next_index += 1 + len(aux) // 18
        return index

    def external(self, name: str) -> int:
        """The index of the undefined external NAME, added on first use."""
        if name not in self._externals:
            found = next((s.index for s in self.symbols if s.name == name and s.storage == EXTERNAL), None)
            self._externals[name] = found if found is not None else self.add_symbol(name, 0, 0, 0, EXTERNAL)
        return self._externals[name]

    # ---------------------------------------------------------------- writing

    def to_bytes(self) -> bytes:
        strings = bytearray(4)

        def long_name(name: bytes) -> int:
            offset = len(strings)
            strings.extend(name + b"\0")
            return offset

        header_size = 20 + 40 * len(self.sections)
        body, headers = bytearray(), bytearray()
        for s in self.sections:
            data_at = header_size + len(body) if s.data else 0
            body += s.data
            relocations_at = header_size + len(body) if s.relocations else 0
            for r in s.relocations:
                body += struct.pack("<IIH", r.offset, r.symbol, r.kind)
            name = s.name.encode("latin-1")
            raw_name = name.ljust(8, b"\0") if len(name) <= 8 else f"/{long_name(name)}".encode().ljust(8, b"\0")
            if len(s.relocations) > 0xFFFF:
                raise CoffError(f"section {s.name}: too many relocations")
            headers += struct.pack(
                "<8sIIIIIIHHI", raw_name, 0, 0, s.size, data_at, relocations_at, 0, len(s.relocations), 0, s.flags
            )
        table = bytearray()
        expected = 0
        for sym in self.symbols:
            if sym.index != expected:
                raise CoffError(f"symbol {sym.name}: index {sym.index}, expected {expected}")
            name = sym.name.encode("latin-1")
            raw_name = name.ljust(8, b"\0") if len(name) <= 8 else struct.pack("<II", 0, long_name(name))
            table += raw_name + struct.pack("<IhHBB", sym.value, sym.section, sym.type, sym.storage, len(sym.aux) // 18)
            table += sym.aux
            expected += sym.records
        struct.pack_into("<I", strings, 0, len(strings))
        header = struct.pack(
            "<HHIIIHH", I386, len(self.sections), self.timestamp, header_size + len(body), expected, 0, 0
        )
        return bytes(header + headers + body + table + strings)

    def write(self, path: Path | str) -> None:
        write_atomic(path, self.to_bytes())


def function_extent(obj: Object, symbol: Symbol) -> tuple[Section, int, int]:
    """(section, start, end) of the function SYMBOL defines: to the next function or external symbol in its section
    (debug and section labels do not end a function), else to the section's end."""
    if not symbol.defined:
        raise CoffError(f"{symbol.name} is not defined in this object")
    section = obj.section(symbol.section)
    ends = [
        s.value
        for s in obj.symbols
        if s.section == symbol.section and s.value > symbol.value and (s.function or s.storage == EXTERNAL)
    ]
    return section, symbol.value, min(ends, default=section.size)
