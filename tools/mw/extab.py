"""The C++ exception tables in the compiler's PowerPC ELF objects (the extab section), and their padding.

Each function's table (src/backend/Exceptions.c, dumpexceptiontables) is a local object of its own in extab, at a
4-aligned offset with its size in the symbol: a header word, a range per covered stretch of code (8 bytes) up to a zero
word, then the action chains, each 4-aligned (zero-filled). An action is a record laid out as the compiler's x86 struct
of its kind, naturally aligned: its first byte the kind (0x80 on a chain's last action), and some kinds leave two bytes
of struct padding. The compiler copies the whole record from a stack local, so those bytes are whatever its stack held
(the original's host garbage, varying with the record, the input and the host: `25 00` in Mario Party 4's retail
objects; `ff 7f` in a catch block, `11 6c` or `5c 00` in others under one wibo, `00 00` under another; zero in the clang
build). decomp-toolkit projects overwrite them per object (`dtk extab clean --padding`); normalize() does so here.

Only the padding is touched. An exception specification without its label (EAT_SPECIFICATION) also leaves a real field
unset (the original's bug: Exceptions.c): that is a value, not padding, and stays as it is.
"""

from __future__ import annotations

import struct

# kind: (the record's size, the offset of its two padding bytes or None); the struct each kind is written as
# (Exceptions.c, allocateactioninfo) is named after its members' sizes: A a byte, B a halfword, C a word
_ACTIONS = {
    0x01: (4, None),  # AAB: branch to another chain
    0x02: (8, None),  # AABC: destroy local
    0x03: (12, 6),  # AABBC: destroy local, conditionally
    0x04: (8, None),  # AABC: destroy local pointer
    0x05: (12, None),  # AABBBC: destroy local array
    0x06: (12, None),  # AABCC: destroy base
    0x07: (12, None),  # AABCC: destroy member
    0x08: (16, 6),  # AABBCC: destroy member, conditionally
    0x09: (20, None),  # AABCCCC: destroy member array
    0x0A: (8, None),  # AABC: delete pointer
    0x0B: (12, 6),  # AABBC: delete pointer, conditionally
    0x0D: (4, None),  # AAB: active catch block
    0x0E: (2, None),  # AA: terminate
    0x0F: (12, None),  # AABCC and a word per type: exception specification
    0x10: (16, 2),  # AACCC: catch block
    # (the 32-bit forms, for a frame whose offsets do not fit 16 bits)
    0x11: (12, 2),  # AACC: destroy local
    0x12: (16, 2),  # AACCC: destroy local, conditionally
    0x13: (12, 2),  # AACC: destroy local pointer
    0x14: (20, 2),  # AACCCC: destroy local array
    0x15: (16, 2),  # AACCC: destroy base
    0x16: (16, 2),  # AACCC: destroy member
    0x17: (20, 2),  # AACCCC: destroy member, conditionally
    0x18: (24, 2),  # AACCCCC: destroy member array
    0x19: (12, 2),  # AACC: delete pointer
    0x1A: (16, 2),  # AACCC: delete pointer, conditionally
    0x1B: (8, 2),  # AAC: active catch block
}
_SPECIFICATION = 0x0F
_BRANCH = 0x01


class ExtabError(ValueError):
    pass


def padding_offsets(table: bytes) -> list[int]:
    """The offsets in one function's exception table of its actions' padding (each the first of two bytes).

    The table must parse exactly as the compiler writes it (zeros between chains, every range and branch naming an
    action), so that nothing set as padding is a value: anything else is an ExtabError."""
    end = len(table)
    # (the ranges: 8 bytes each after the header word, up to a zero word: a range's code offset is never 0)
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
            # (each chain starts 4-aligned, after zeros)
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
        # (a chain ends at the action marked last, or at a branch into another chain)
        chain_start = bool(table[at] & 0x80) or kind == _BRANCH
        at += size
    for target in targets:
        if target not in starts:
            raise ExtabError(f"a range or branch names {target:#x}, which is not an action")
    return offsets


def _sections(data: bytes) -> list[tuple[str, int, int, int]]:
    """A big-endian ELF32 object's sections: (name, type, file offset, size) each."""
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
        if kind != 8 and offset + size > len(data):  # (SHT_NOBITS has no bytes in the file)
            raise ExtabError(f"a malformed ELF object: its section {name} runs past the file's end")
    return sections


def tables(data: bytes) -> list[tuple[int, int]]:
    """Each exception table in the ELF object DATA: (its file offset, its size), from the local objects the compiler
    defines in extab."""
    sections = _sections(data)
    extab = {i for i, s in enumerate(sections) if s[0] in ("extab", ".extab")}
    found = set()
    for _name, kind, offset, size in sections if extab else ():
        if kind != 2:  # SHT_SYMTAB
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
    """DATA, an ELF object, with every exception action's padding set to PADDING (two bytes); and how many bytes that
    changed. An object without exception tables comes back as it is."""
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
    """The padding as decomp-toolkit spells it: two bytes in hexadecimal (`2500`)."""
    try:
        padding = bytes.fromhex(text)
    except ValueError:
        padding = b""
    if len(padding) != 2:
        raise ExtabError(f"{text!r} is not two bytes in hexadecimal (e.g. 2500)")
    return padding
