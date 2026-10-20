from __future__ import annotations

import struct

_ACTIONS = {
    0x01: (4, None),
    0x02: (8, None),
    0x03: (12, 6),
    0x04: (8, None),
    0x05: (12, None),
    0x06: (12, None),
    0x07: (12, None),
    0x08: (16, 6),
    0x09: (20, None),
    0x0A: (8, None),
    0x0B: (12, 6),
    0x0D: (4, None),
    0x0E: (2, None),
    0x0F: (12, None),
    0x10: (16, 2),
    0x11: (12, 2),
    0x12: (16, 2),
    0x13: (12, 2),
    0x14: (20, 2),
    0x15: (16, 2),
    0x16: (16, 2),
    0x17: (20, 2),
    0x18: (24, 2),
    0x19: (12, 2),
    0x1A: (16, 2),
    0x1B: (8, 2),
}
_SPECIFICATION = 0x0F
_BRANCH = 0x01


class ExtabError(ValueError):
    pass


def padding_offsets(table: bytes) -> list[int]:
    end = len(table)
    at = 4
    targets = []
    while True:
        if at + 4 > end:
            raise ExtabError("the range list has no end")
        if struct.unpack_from(">I", table, at)[0] == 0:
            at += 4
            break
        if at + 8 > end:
            raise ExtabError(f"the range at {at:#x} runs past the table's end")
        targets.append(struct.unpack_from(">H", table, at + 6)[0])
        at += 8
    offsets = []
    starts = set()
    chain_start = True
    while at < end:
        if chain_start and at & 3:
            gap = (at + 3) & ~3
            if any(table[at:gap]):
                raise ExtabError(f"non-zero bytes between chains at {at:#x}")
            at = gap
            if at >= end:
                break
        kind = table[at] & 0x7F
        if kind not in _ACTIONS:
            raise ExtabError(f"an action of unknown kind {table[at]:#04x} at {at:#x}")
        size, padding = _ACTIONS[kind]
        if at + min(size, 4) > end:
            raise ExtabError(f"an action of kind {kind:#04x} at {at:#x} runs past the table's end")
        if kind == _SPECIFICATION:
            size += 4 * struct.unpack_from(">H", table, at + 2)[0]
        elif kind == _BRANCH:
            targets.append(struct.unpack_from(">H", table, at + 2)[0])
        if at + size > end:
            raise ExtabError(f"an action of kind {kind:#04x} at {at:#x} runs past the table's end")
        if padding is not None:
            offsets.append(at + padding)
        starts.add(at)
        chain_start = bool(table[at] & 0x80) or kind == _BRANCH
        at += size
    for target in targets:
        if target not in starts:
            raise ExtabError(f"a range or branch names {target:#x}, which is not an action")
    return offsets


def _sections(data: bytes) -> list[tuple[str, int, int, int]]:
    if data[:4] != b"\x7fELF" or data[4:6] != b"\x01\x02":
        raise ExtabError("not a big-endian ELF32 object")
    try:
        (shoff,) = struct.unpack_from(">I", data, 0x20)
        shentsize, shnum, shstrndx = struct.unpack_from(">HHH", data, 0x2E)
        headers = [struct.unpack_from(">IIIIII", data, shoff + i * shentsize) for i in range(shnum)]
        names = headers[shstrndx][4]
        sections = [
            (data[names + h[0] : data.index(b"\0", names + h[0])].decode("latin-1"), h[1], h[4], h[5]) for h in headers
        ]
    except (struct.error, ValueError, IndexError):
        raise ExtabError("a malformed ELF object") from None
    for name, kind, offset, size in sections:
        if kind != 8 and offset + size > len(data):
            raise ExtabError(f"a malformed ELF object: its section {name} runs past the file's end")
    return sections


def tables(data: bytes) -> list[tuple[int, int]]:
    sections = _sections(data)
    extab = {i for i, s in enumerate(sections) if s[0] in ("extab", ".extab")}
    found = set()
    for _name, kind, offset, size in sections if extab else ():
        if kind != 2:
            continue
        for at in range(offset, offset + size - 15, 16):
            value, length, _info, _other, shndx = struct.unpack_from(">IIBBH", data, at + 4)
            if shndx in extab and length:
                start = sections[shndx][2] + value
                if value + length > sections[shndx][3]:
                    raise ExtabError(f"an exception table at {value:#x} runs past its section's end")
                found.add((start, length))
    return sorted(found)


def normalize(data: bytes, padding: bytes = b"\0\0") -> tuple[bytes, int]:
    if len(padding) != 2:
        raise ExtabError("the padding is two bytes")
    if data[:4] != b"\x7fELF":
        return data, 0
    out = bytearray(data)
    changed = 0
    for offset, size in tables(data):
        for at in padding_offsets(data[offset : offset + size]):
            for i in range(2):
                if out[offset + at + i] != padding[i]:
                    out[offset + at + i] = padding[i]
                    changed += 1
    return bytes(out), changed


def parse_padding(text: str) -> bytes:
    try:
        padding = bytes.fromhex(text)
    except ValueError:
        padding = b""
    if len(padding) != 2:
        raise ExtabError(f"{text!r} is not two bytes in hexadecimal (e.g. 2500)")
    return padding
