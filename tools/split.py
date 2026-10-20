#!/usr/bin/env python3
"""python tools/split.py VERSION: the original executable as one COFF object per unit (build/VERSION/obj), like
decomp-toolkit's `dol split`, from config/VERSION/symbols.txt and config/VERSION/splits.txt (its formats).

Absolute references are the original's base relocations; relative ones are the rel32 calls and jumps out of a function.
A reference's target is the symbol containing it; one in a section no unit holds (.idata) is an external by name. Code is
a section per function, as the compiler writes it. sections.obj, which the link starts with, gives the linker the
original's order of sections and imports; resources.res the original's resources. build/VERSION/obj/ok records the
inputs the objects were made from (tools/relink.py refuses objects older than the configuration).
"""

import json
import struct
import sys
from bisect import bisect_right
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import formats, project
from mw.coff import EXTERNAL, STATIC, Object, Relocation
from mw.pe import original

SECTION_FLAGS = {"code": 0x60000020, "data": 0xC0000040, "rodata": 0x40000040, "bss": 0xC0000080}
WEAK_EXTERNAL = 105


def inputs(version):
    """{path: SHA-1} of what the split objects are made from."""
    v = project.version(version)
    paths = [v.config / "symbols.txt", v.config / "splits.txt"]
    return {project.relative(p): project.digest(p.read_bytes()) for p in paths}


# Instruction lengths of 32-bit x86 code. One-byte map: opcode -> (has ModRM, immediate), the immediate a size in
# bytes, "z" (operand-sized), "a" (address-sized), "p" (far pointer) or "grp3" (test's immediate in group 3).
ONE_BYTE = {}
for base in range(0x00, 0x40, 8):
    ONE_BYTE.update(
        {
            base: (1, 0),
            base + 1: (1, 0),
            base + 2: (1, 0),
            base + 3: (1, 0),
            base + 4: (0, 1),
            base + 5: (0, "z"),
            base + 6: (0, 0),
            base + 7: (0, 0),
        }
    )
ONE_BYTE.update({opcode: (0, 0) for opcode in range(0x40, 0x62)})
ONE_BYTE.update(
    {
        0x62: (1, 0),
        0x63: (1, 0),
        0x68: (0, "z"),
        0x69: (1, "z"),
        0x6A: (0, 1),
        0x6B: (1, 1),
        0x6C: (0, 0),
        0x6D: (0, 0),
        0x6E: (0, 0),
        0x6F: (0, 0),
    }
)
ONE_BYTE.update({opcode: (0, 1) for opcode in range(0x70, 0x80)})
ONE_BYTE.update({0x80: (1, 1), 0x81: (1, "z"), 0x82: (1, 1), 0x83: (1, 1)})
ONE_BYTE.update({opcode: (1, 0) for opcode in range(0x84, 0x90)})
ONE_BYTE.update({opcode: (0, 0) for opcode in range(0x90, 0xA0)})
ONE_BYTE.update(
    {0x9A: (0, "p"), 0xA0: (0, "a"), 0xA1: (0, "a"), 0xA2: (0, "a"), 0xA3: (0, "a"), 0xA8: (0, 1), 0xA9: (0, "z")}
)
ONE_BYTE.update({opcode: (0, 0) for opcode in (*range(0xA4, 0xA8), *range(0xAA, 0xB0))})
ONE_BYTE.update({opcode: (0, 1) for opcode in range(0xB0, 0xB8)})
ONE_BYTE.update({opcode: (0, "z") for opcode in range(0xB8, 0xC0)})
ONE_BYTE.update(
    {
        0xC0: (1, 1),
        0xC1: (1, 1),
        0xC2: (0, 2),
        0xC3: (0, 0),
        0xC4: (1, 0),
        0xC5: (1, 0),
        0xC6: (1, 1),
        0xC7: (1, "z"),
        0xC8: (0, 3),
        0xC9: (0, 0),
        0xCA: (0, 2),
        0xCB: (0, 0),
        0xCC: (0, 0),
        0xCD: (0, 1),
        0xCE: (0, 0),
        0xCF: (0, 0),
        0xD0: (1, 0),
        0xD1: (1, 0),
        0xD2: (1, 0),
        0xD3: (1, 0),
        0xD4: (0, 1),
        0xD5: (0, 1),
        0xD6: (0, 0),
        0xD7: (0, 0),
    }
)
ONE_BYTE.update({opcode: (1, 0) for opcode in range(0xD8, 0xE0)})
ONE_BYTE.update({opcode: (0, 1) for opcode in range(0xE0, 0xE8)})
ONE_BYTE.update(
    {
        0xE8: (0, "z"),
        0xE9: (0, "z"),
        0xEA: (0, "p"),
        0xEB: (0, 1),
        0xF1: (0, 0),
        0xF4: (0, 0),
        0xF5: (0, 0),
        0xF6: (1, "grp3"),
        0xF7: (1, "grp3"),
        0xFE: (1, 0),
        0xFF: (1, 0),
    }
)
ONE_BYTE.update({opcode: (0, 0) for opcode in (*range(0xEC, 0xF0), *range(0xF8, 0xFE))})
TWO_BYTE = {
    0x00: (1, 0),
    0x01: (1, 0),
    0x02: (1, 0),
    0x03: (1, 0),
    0x06: (0, 0),
    0x08: (0, 0),
    0x09: (0, 0),
    0x0B: (0, 0),
    0x0D: (1, 0),
    0x0E: (0, 0),
    0x0F: (1, 1),
    0x1F: (1, 0),
    0x31: (0, 0),
    0x77: (0, 0),
    0xA0: (0, 0),
    0xA1: (0, 0),
    0xA2: (0, 0),
    0xA3: (1, 0),
    0xA4: (1, 1),
    0xA5: (1, 0),
    0xA8: (0, 0),
    0xA9: (0, 0),
    0xAB: (1, 0),
    0xAC: (1, 1),
    0xAD: (1, 0),
    0xAF: (1, 0),
    0xB0: (1, 0),
    0xB1: (1, 0),
    0xB3: (1, 0),
    0xB6: (1, 0),
    0xB7: (1, 0),
    0xBA: (1, 1),
    0xBB: (1, 0),
    0xBC: (1, 0),
    0xBD: (1, 0),
    0xBE: (1, 0),
    0xBF: (1, 0),
    0xC0: (1, 0),
    0xC1: (1, 0),
    0xC7: (1, 0),
}
TWO_BYTE.update(
    {
        opcode: (1, 0)
        for opcode in (
            *range(0x10, 0x18),
            *range(0x20, 0x24),
            *range(0x28, 0x30),
            *range(0x40, 0x70),
            *range(0x74, 0x77),
            0x7E,
            0x7F,
            *range(0x90, 0xA0),
            *range(0xD0, 0x100),
        )
    }
)
TWO_BYTE.update({opcode: (1, 1) for opcode in range(0x70, 0x74)})
TWO_BYTE.update({opcode: (0, "z") for opcode in range(0x80, 0x90)})
TWO_BYTE.update({opcode: (0, 0) for opcode in range(0xC8, 0xD0)})
PREFIXES = {0x26, 0x2E, 0x36, 0x3E, 0x64, 0x65, 0x66, 0x67, 0xF0, 0xF2, 0xF3}


def branches(code, base):
    """(offset of the displacement, target) of each call, jmp and jcc with a 32-bit displacement in CODE at BASE."""
    out, offset = [], 0
    while offset < len(code):
        start, operand16, address16 = offset, False, False
        while code[offset] in PREFIXES:
            operand16 |= code[offset] == 0x66
            address16 |= code[offset] == 0x67
            offset += 1
        opcode = code[offset]
        offset += 1
        if opcode == 0x0F:
            opcode = 0x0F00 | code[offset]
            offset += 1
            modrm, immediate = TWO_BYTE[opcode & 0xFF]
        else:
            modrm, immediate = ONE_BYTE[opcode]
        if modrm:
            byte = code[offset]
            offset += 1
            mod, rm = byte >> 6, byte & 7
            if immediate == "grp3":
                immediate = (1 if opcode == 0xF6 else "z") if (byte >> 3) & 7 in (0, 1) else 0
            if address16:
                offset += {0: 2 if rm == 6 else 0, 1: 1, 2: 2, 3: 0}[mod]
            else:
                if mod != 3 and rm == 4:
                    offset += 1 + (4 if mod == 0 and code[offset] & 7 == 5 else 0)
                offset += {0: 4 if rm == 5 else 0, 1: 1, 2: 4, 3: 0}[mod]
        field = offset
        relative = opcode in (0xE8, 0xE9) or 0x0F80 <= opcode <= 0x0F8F
        if immediate == "z":
            offset += 2 if operand16 and not relative else 4
        elif immediate == "a":
            offset += 2 if address16 else 4
        else:
            offset += 6 if immediate == "p" else immediate
        if relative:
            displacement = int.from_bytes(code[field : field + 4], "little", signed=True)
            out.append((field - start + start, base + offset + displacement))
    return out


def mwld_hash(name):
    """The hash the linker orders COMMON symbols of one size by (of the C name)."""
    x = 0
    for c in name.removeprefix("_").encode("latin-1"):
        x = (x << 1) ^ c
        x = (x & 0xFFF) ^ (x >> 12)
    return x


def common_layout(entries, start):
    """The addresses the linker gives COMMON symbols [(name, size)] from START: by the alignment their sizes give them
    (the largest power of two dividing the size, at most 8), the largest first, then by hash."""
    layout, cursor = {}, start
    alignment = lambda size: min(size & -size, 8)
    for name, size in sorted(entries, key=lambda entry: (-alignment(entry[1]), mwld_hash(entry[0]))):
        align = alignment(size)
        cursor = (cursor + align - 1) & ~(align - 1)
        layout[name] = cursor
        cursor += size
    return layout


def common_names(count):
    """COUNT names for one-byte COMMON symbols the linker places in their order (increasing hashes)."""
    names, last = [], -1
    for i in range(count):
        suffix = 0
        while mwld_hash(name := f"_common_{i}_{suffix}") <= last:
            suffix += 1
        names.append(name)
        last = mwld_hash(name)
    return names


def resource_file(pe, section):
    """The .res file (the resource compiler's output) whose resources are those of SECTION: its directory's leaves, in
    the order the linker lays out their data (string tables, then the rest by type)."""
    data = pe.read(section.virtual_address, section.virtual_size)

    def entries(offset):
        _, _, _, _, named, numbered = struct.unpack_from("<IIHHHH", data, offset)
        for i in range(named + numbered):
            key, target = struct.unpack_from("<II", data, offset + 16 + 8 * i)
            if key & 0x80000000:
                at = key & 0x7FFFFFFF
                key = data[at + 2 : at + 2 + 2 * struct.unpack_from("<H", data, at)[0]].decode("utf-16le")
            yield key, target & 0x7FFFFFFF

    leaves = []
    for kind, names in entries(0):
        for name, languages in entries(names):
            for language, leaf in entries(languages):
                rva, size, _, _ = struct.unpack_from("<IIII", data, leaf)
                leaves.append((rva, kind, name, language, pe.read(pe.image_base + rva, size)))

    def field(key):
        return key.encode("utf-16le") + b"\0\0" if isinstance(key, str) else struct.pack("<HH", 0xFFFF, key)

    out = bytearray(struct.pack("<IIHHHHIHHII", 0, 32, 0xFFFF, 0, 0xFFFF, 0, 0, 0, 0, 0, 0))
    for _, kind, name, language, body in sorted(leaves):
        header = field(kind) + field(name)
        header += b"\0" * (-len(header) % 4)
        # (memory flags, which the image does not keep: moveable, pure, discardable)
        header += struct.pack("<IHHII", 0, 0x1030, language, 0, 0)
        out += struct.pack("<II", len(body), 8 + len(header)) + header + body
        out += b"\0" * (-len(out) % 4)
    return bytes(out)


def split(version):
    v = project.version(version, original=True)
    pe = original(v)
    symbols = [(s.name, s.section, s.address, dict(s.options)) for s in formats.read_symbols(v.config / "symbols.txt")]
    splits = formats.read_splits(v.config / "splits.txt")
    sections = {name: dict(options) for name, options in splits.sections.items()}
    units = [(u.name, [(r.section, r.start, r.end, dict(r.options)) for r in u.ranges]) for u in splits.units]
    fixups = pe.base_relocations
    out = v.build / "obj"

    # (the unit whose range holds each symbol)
    ranges_by_section = {}
    for unit, ranges in units:
        for section, start, end, _ in ranges:
            ranges_by_section.setdefault(section, []).append((start, end, unit))
    for section in ranges_by_section.values():
        section.sort()

    def unit_at(section, address):
        for start, end, unit in ranges_by_section.get(section, []):
            if start <= address < end or start == address == end:
                return unit
        return None

    targets = sorted(
        (
            (address, int(options.get("size", "0"), 16), name, section, options, unit_at(section, address))
            for name, section, address, options in symbols
            if options.get("type") != "label"
        ),
        key=lambda entry: entry[0],
    )
    starts = [entry[0] for entry in targets]

    def pe_section(value):
        try:
            return pe.section_for_address(value)
        except ValueError:
            return None

    def resolve(value, unit):
        """The symbol a reference from UNIT to VALUE is written against: one of UNIT's or a global one containing it,
        else (a constant offset out of an object, such as a[-1]) the nearest of UNIT's symbols in that section."""
        hi = bisect_right(starts, value)
        containing = [t for t in targets[max(0, hi - 64) : hi] if t[0] == value or t[0] <= value < t[0] + t[1]]
        usable = [t for t in containing if t[5] == unit or t[4].get("scope") != "local"]
        if usable:
            best = max(usable, key=lambda t: (t[0], t[5] == unit))
            return best[2], best[3], best[0], best[4]
        try:
            section = pe.section_for_address(value).name
        except ValueError:
            return None
        own = [t for t in targets[max(0, hi - 512) : hi + 512] if t[3] == section and t[5] == unit]
        if not own:
            return None
        best = min(own, key=lambda t: abs(t[0] - value))
        return best[2], best[3], best[0], best[4]

    # (where each function's code ends: its size, or up to the next function when what is between is not padding)
    code = sorted(
        {
            (address, int(options["size"], 16))
            for _, section, address, options in symbols
            if section == ".text" and options.get("type") == "function"
        }
    )
    code_ends = {}
    for i, (address, size) in enumerate(code):
        following = code[i + 1][0] if i + 1 < len(code) else address + size
        stop = address + size
        if following - stop >= 16 or any(pe.read(stop, following - stop)):
            stop = following
        code_ends[following] = stop
    for unit, ranges in units:
        coff = Object()
        names = {}  # (the externals by name, weak ones included)
        pending = []  # (each section's relocations, by target: resolved to indices once every symbol is added)
        local = {}
        defined = []
        # (the compiler writes a reference to a static in .bss against the section, as it names none of them)
        bss_start = next((start for section, start, _, _ in ranges if section == ".bss"), None)
        for section, start, end, options in ranges:
            if options.get("common"):
                # (left to the linker's COMMON allocation, as the original's last variables were)
                tail = [
                    (name, address, int(symbol_options["size"], 16))
                    for name, sec, address, symbol_options in symbols
                    if sec == section and start <= address < end
                ]
                if all(common_layout([(n, size) for n, _, size in tail], start)[n] == a for n, a, _ in tail):
                    for name, address, size in tail:
                        local[(name, address)] = coff.add_symbol(name, size, 0, 0, EXTERNAL)
                else:
                    # (names that the linker would order otherwise than the original's were: a one-byte COMMON per byte,
                    # named to keep their order, and each name a weak external aliasing its byte)
                    slots = [coff.add_symbol(name, 1, 0, 0, EXTERNAL) for name in common_names(end - start)]
                    for name, address, _ in tail:
                        local[(name, address)] = names[name] = coff.add_symbol(
                            name, 0, 0, 0, WEAK_EXTERNAL, struct.pack("<II10x", slots[address - start], 3)
                        )
                continue
            kind = options.get("type", sections[section].get("type"))
            align = int(options.get("align", sections[section].get("align", "4")))
            flags = SECTION_FLAGS[kind] | (align.bit_length() << 20)
            members = [
                (name, address, options)
                for name, sec, address, options in symbols
                if sec == section
                and (start <= address < end or start == address == end)
                and unit_at(section, address) == unit
            ]
            # (code is a section per function, as the compiler writes it; the zeros up to the next function are the
            # linker's alignment, any other bytes between functions the first's)
            pieces = [(start, end)]
            # (one piece per function address: an alias, another name of a function, adds none)
            functions = sorted(
                {
                    (address, int(options["size"], 16))
                    for _, address, options in members
                    if options.get("type") == "function"
                }
            )
            if kind == "code" and functions and functions[0][0] == start:
                pieces = []
                for i, (address, size) in enumerate(functions):
                    following = functions[i + 1][0] if i + 1 < len(functions) else end
                    stop = address + size
                    if following - stop >= 16 or any(pe.read(stop, following - stop)):
                        stop = following
                    pieces.append((address, stop))
            for piece_start, piece_end in pieces:
                if kind == "code":
                    # (the linker starts a function where the previous one's code ends, at the section's alignment:
                    # the largest that puts it at its address)
                    before = code_ends.get(piece_start, piece_start)
                    align = max(
                        (a for a in (16, 8, 4, 2, 1) if piece_start % a == 0 and before <= piece_start < before + a),
                        default=1,
                    )
                    flags = SECTION_FLAGS[kind] | (align.bit_length() << 20)
                data = b"" if kind == "bss" else pe.read(piece_start, piece_end - piece_start)
                relocations = []
                content = bytearray(data)
                if kind != "bss":
                    lo = bisect_right(fixups, piece_start - 1)
                    for at in fixups[lo : bisect_right(fixups, piece_end - 1)]:
                        value = struct.unpack_from("<I", pe.data, pe.address_to_offset(at))[0]
                        target = resolve(value, unit)
                        holder = [m for m in members if m[1] <= at and piece_start <= m[1] < piece_end]
                        if target is None and pe_section(value) is None and holder:
                            # (a fixup the executable keeps over bytes patched by hand, not an address: written against
                            # the symbol holding it, the addend making up the value)
                            name, address, symbol_options = max(holder, key=lambda m: m[1])
                            target = name, section, address, symbol_options
                        if target is None:
                            raise project.Error(f"{unit}: no symbol for 0x{value:08x} referenced at 0x{at:08x}")
                        if (
                            target[1] == ".bss"
                            and target[3].get("scope") == "local"
                            and bss_start is not None
                            and unit_at(".bss", target[2]) == unit
                        ):
                            target = ".bss", ".bss", bss_start, {"scope": "local"}
                        struct.pack_into("<I", content, at - piece_start, (value - target[2]) & 0xFFFFFFFF)
                        relocations.append((at - piece_start, target, 6))
                if kind == "code":
                    scanned = set()
                    for name, address, options in members:
                        if (
                            options.get("type") != "function"
                            or not piece_start <= address < piece_end
                            or address in scanned
                        ):
                            continue
                        scanned.add(address)
                        size = int(options["size"], 16)
                        for field, value in branches(pe.read(address, size), address):
                            if address <= value < address + size:
                                continue
                            target = resolve(value, unit)
                            if target is None:
                                raise project.Error(
                                    f"{unit}: branch at 0x{address + field:08x} to 0x{value:08x} is not to a symbol"
                                )
                            # (a branch into a function, which hand patches make, keeps its offset as the addend)
                            struct.pack_into(
                                "<I", content, address + field - piece_start, (value - target[2]) & 0xFFFFFFFF
                            )
                            relocations.append((address + field - piece_start, target, 20))
                number = coff.add_section(
                    options.get("rename", section),
                    flags,
                    bytes(content) if kind != "bss" else b"",
                    piece_end - piece_start,
                    [Relocation(0, 0, 0)] * len(relocations),
                ).number
                pending.append(relocations)
                if kind == "bss" and section == ".bss":
                    local[(".bss", piece_start)] = coff.symbols[-1].index
                for name, address, symbol_options in members:
                    if not (piece_start <= address < piece_end or piece_start == address == piece_end):
                        continue
                    scope = symbol_options.get("scope", "global")
                    if kind == "bss" and scope == "local":
                        continue
                    kind_bits = 0x20 if symbol_options.get("type") == "function" else 0
                    index = coff.add_symbol(
                        name, address - piece_start, number, kind_bits, STATIC if scope == "local" else EXTERNAL
                    )
                    local[(name, address)] = index
                    defined.append(name)
        # (relocations name their targets: a symbol of this unit by index, any other by name)
        for section, relocations in zip(coff.sections, pending):
            section.relocations = []
            for offset, (target, _, address, options), kind in relocations:
                index = local.get((target, address))
                if index is None:
                    if options.get("scope") == "local":
                        raise project.Error(f"{unit}: reference to {target}, local to another unit")
                    if target not in names:
                        names[target] = coff.add_symbol(target, 0, 0, 0, EXTERNAL)
                    index = names[target]
                section.relocations.append(Relocation(offset, index, kind))
        coff.write(out / f"{unit}.obj")
    # (the linker orders the image's sections as they first appear and gives imports their slots in the order objects
    # first name them, dead code included: this object, linked first, lists both in the original's order)
    order = Object()
    for section, options in sections.items():
        order.add_section(section, SECTION_FLAGS[options["type"]] | (int(options.get("align", "4")).bit_length() << 20))
    for _, name in sorted((address, name) for name, section, address, _ in symbols if section == ".idata"):
        order.external(name)
    order.write(out / "sections.obj")
    # (the resources: the original's .rsrc as the .res file the linker builds it from)
    resources = next((s for s in pe.sections if s.name == ".rsrc"), None)
    if resources:
        project.write_atomic(out / "resources.res", resource_file(pe, resources))
    project.write_atomic(out / "ok", json.dumps(dict(inputs=inputs(v)), indent=1) + "\n")


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    if len(sys.argv) != 2 or sys.argv[1].startswith("-"):
        sys.exit(__doc__.strip())
    try:
        split(sys.argv[1])
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
