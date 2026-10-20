"""DWARF 4 for a 32-bit x86 executable, from the CodeView 8 types tools/mw/codeview.py makes and a neutral description of
the functions and variables (tools/link_debug.py): .debug_abbrev, .debug_info, .debug_line and .debug_str, for the
executable's own sections (as MinGW's executables have them), which Ghidra, IDA and Binary Ninja read.

- one compile unit holds the types, one per translation unit the rest; a type is referred to by its offset in
  .debug_info (DW_FORM_ref_addr) from any of them. A forward reference is its name's definition: DWARF refers to a
  type by its place, so a structure can refer to itself.
- a function's frame base is the call frame address (DW_OP_call_frame_cfa: the stack pointer before the call pushed
  the return address); an argument at the compiler's virtual frame offset OFFSET (the first at 8) is at CFA +
  OFFSET - 8.
- a variable in a register is DW_OP_reg; one relative to the stack pointer after the prologue is placed from the
  prologue's pushes and stack adjustment."""

import struct

# DWARF constants
DW_TAG = dict(
    array_type=0x01,
    enumeration_type=0x04,
    formal_parameter=0x05,
    member=0x0D,
    pointer_type=0x0F,
    compile_unit=0x11,
    structure_type=0x13,
    subroutine_type=0x15,
    union_type=0x17,
    unspecified_parameters=0x18,
    inheritance=0x1C,
    subrange_type=0x21,
    base_type=0x24,
    enumerator=0x28,
    subprogram=0x2E,
    variable=0x34,
)
DW_AT = dict(
    location=0x02,
    name=0x03,
    byte_size=0x0B,
    stmt_list=0x10,
    low_pc=0x11,
    high_pc=0x12,
    language=0x13,
    comp_dir=0x1B,
    const_value=0x1C,
    producer=0x25,
    prototyped=0x27,
    count=0x37,
    data_member_location=0x38,
    decl_file=0x3A,
    decl_line=0x3B,
    declaration=0x3C,
    encoding=0x3E,
    external=0x3F,
    frame_base=0x40,
    type=0x49,
    bit_size=0x0D,
    data_bit_offset=0x6B,
)
DW_FORM = dict(
    addr=0x01,
    data2=0x05,
    data4=0x06,
    data1=0x0B,
    flag=0x0C,
    sdata=0x0D,
    strp=0x0E,
    udata=0x0F,
    ref_addr=0x10,
    sec_offset=0x17,
    exprloc=0x18,
    flag_present=0x19,
)
DW_OP_addr, DW_OP_reg0, DW_OP_fbreg, DW_OP_call_frame_cfa = 0x03, 0x50, 0x91, 0x9C
DW_ATE_boolean, DW_ATE_float, DW_ATE_signed, DW_ATE_signed_char, DW_ATE_unsigned, DW_ATE_unsigned_char = (
    0x02,
    0x04,
    0x05,
    0x06,
    0x08,
    0x08,
)

# CodeView primitive types (the low byte of a primitive type index): (name, encoding, size)
PRIMITIVES = {
    0x10: ("signed char", DW_ATE_signed_char, 1),
    0x70: ("char", DW_ATE_signed_char, 1),
    0x20: ("unsigned char", DW_ATE_unsigned_char, 1),
    0x68: ("__int8", DW_ATE_signed, 1),
    0x69: ("unsigned __int8", DW_ATE_unsigned, 1),
    0x11: ("short", DW_ATE_signed, 2),
    0x72: ("short", DW_ATE_signed, 2),
    0x21: ("unsigned short", DW_ATE_unsigned, 2),
    0x73: ("unsigned short", DW_ATE_unsigned, 2),
    0x71: ("wchar_t", DW_ATE_unsigned, 2),
    0x12: ("long", DW_ATE_signed, 4),
    0x22: ("unsigned long", DW_ATE_unsigned, 4),
    0x74: ("int", DW_ATE_signed, 4),
    0x75: ("unsigned int", DW_ATE_unsigned, 4),
    0x13: ("__int64", DW_ATE_signed, 8),
    0x23: ("unsigned __int64", DW_ATE_unsigned, 8),
    0x76: ("__int64", DW_ATE_signed, 8),
    0x77: ("unsigned __int64", DW_ATE_unsigned, 8),
    0x40: ("float", DW_ATE_float, 4),
    0x41: ("double", DW_ATE_float, 8),
    0x42: ("long double", DW_ATE_float, 10),
    0x30: ("bool", DW_ATE_boolean, 1),
    0x31: ("bool16", DW_ATE_boolean, 2),
    0x32: ("bool32", DW_ATE_boolean, 4),
}
# CodeView x86 registers to DWARF's (a part of a register is the register)
REGISTERS = {
    1: 0,
    2: 1,
    3: 2,
    4: 3,
    5: 0,
    6: 1,
    7: 2,
    8: 3,
    9: 0,
    10: 1,
    11: 2,
    12: 3,
    13: 4,
    14: 5,
    15: 6,
    16: 7,
    17: 0,
    18: 1,
    19: 2,
    20: 3,
    21: 4,
    22: 5,
    23: 6,
    24: 7,
}


def uleb(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        out.append(byte | (0x80 if value else 0))
        if not value:
            return bytes(out)


def sleb(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        done = value == 0 and not byte & 0x40 or value == -1 and byte & 0x40
        out.append(byte | (0 if done else 0x80))
        if done:
            return bytes(out)


def numeric(body, at):
    """(the value of the CodeView numeric leaf at AT, the offset after it)."""
    value = struct.unpack_from("<H", body, at)[0]
    if value < 0x8000:
        return value, at + 2
    form = {0x8000: "<b", 0x8001: "<h", 0x8002: "<H", 0x8003: "<i", 0x8004: "<I", 0x8009: "<q", 0x800A: "<Q"}[value]
    return struct.unpack_from(form, body, at + 2)[0], at + 2 + struct.calcsize(form)


def cstring(body, at):
    end = body.index(0, at)
    return body[at:end].decode("latin-1"), end + 1


class Types:
    """The CodeView 8 type records RECORDS (each with its length and leaf), read."""

    def __init__(self, records):
        self.records = {0x1000 + i: (struct.unpack_from("<H", r, 2)[0], r[4:]) for i, r in enumerate(records)}
        # (a forward reference: its name's definition)
        definitions = {}
        for index, (leaf, body) in self.records.items():
            if leaf in (0x1504, 0x1505, 0x1506, 0x1507) and not self.forward(index):
                definitions.setdefault(self.name(index), index)
        self.definition = {
            index: definitions.get(self.name(index), index)
            for index, (leaf, _) in self.records.items()
            if leaf in (0x1504, 0x1505, 0x1506, 0x1507) and self.forward(index)
        }

    def forward(self, index):
        return bool(struct.unpack_from("<H", self.records[index][1], 2)[0] & 0x80)

    def name(self, index):
        leaf, body = self.records[index]
        if leaf == 0x1507:
            return cstring(body, 12)[0]
        return cstring(body, numeric(body, 8 if leaf == 0x1506 else 16)[1])[0]

    def resolve(self, index):
        return self.definition.get(index, index)

    def size(self, index):
        """The size in bytes of the type INDEX (0 if unknown)."""
        if index < 0x1000:
            if (index >> 8) & 7:
                return 4
            return PRIMITIVES.get(index & 0xFF, ("", 0, 0))[2]
        index = self.resolve(index)
        leaf, body = self.records.get(index, (0, b""))
        if leaf == 0x1002:
            return 4
        if leaf in (0x1504, 0x1505):
            return numeric(body, 16)[0]
        if leaf == 0x1506:
            return numeric(body, 8)[0]
        if leaf == 0x1503:
            return numeric(body, 8)[0]
        if leaf == 0x1507:
            return self.size(struct.unpack_from("<I", body, 4)[0])
        return 0

    def fields(self, index):
        """[(leaf, name, type, offset or value)] of the field list INDEX."""
        if index not in self.records or self.records[index][0] != 0x1203:
            return []
        body, at, out = self.records[index][1], 0, []
        while at < len(body):
            if body[at] >= 0xF0:
                at += body[at] & 0x0F
                continue
            leaf = struct.unpack_from("<H", body, at)[0]
            if leaf == 0x150D:  # LF_MEMBER
                kind = struct.unpack_from("<I", body, at + 4)[0]
                offset, at = numeric(body, at + 8)
                name, at = cstring(body, at)
                out.append((leaf, name, kind, offset))
            elif leaf == 0x1502:  # LF_ENUMERATE
                value, at = numeric(body, at + 4)
                name, at = cstring(body, at)
                out.append((leaf, name, None, value))
            elif leaf == 0x1400:  # LF_BCLASS
                kind = struct.unpack_from("<I", body, at + 4)[0]
                offset, at = numeric(body, at + 8)
                out.append((leaf, "", kind, offset))
            else:
                raise ValueError(f"field leaf 0x{leaf:04x}")
        return out

    def arguments(self, index):
        """(the return type, [argument types], variadic) of the procedure type INDEX, or None."""
        if index not in self.records or self.records[index][0] != 0x1008:
            return None
        body = self.records[index][1]
        result, arglist = struct.unpack_from("<I", body, 0)[0], struct.unpack_from("<I", body, 8)[0]
        kinds = []
        if arglist in self.records and self.records[arglist][0] == 0x1201:
            args = self.records[arglist][1]
            kinds = list(struct.unpack_from(f"<{struct.unpack_from('<I', args, 0)[0]}I", args, 4))
        variadic = bool(kinds) and kinds[-1] == 0
        return result, [k for k in kinds if k], variadic


class Abbreviations:
    def __init__(self):
        self.table, self.codes = bytearray(), {}

    def code(self, tag, children, attributes):
        key = (tag, children, tuple(attributes))
        if key not in self.codes:
            self.codes[key] = len(self.codes) + 1
            self.table += uleb(self.codes[key]) + uleb(DW_TAG[tag]) + bytes([1 if children else 0])
            for attribute, form in attributes:
                self.table += uleb(DW_AT[attribute]) + uleb(DW_FORM[form])
            self.table += b"\0\0"
        return self.codes[key]


class Writer:
    """DWARF sections being written: .debug_info as compile units of DIEs."""

    def __init__(self):
        self.abbreviations = Abbreviations()
        self.info = bytearray()
        self.strings, self.string_offsets = bytearray(), {}
        self.lines = bytearray()
        self.fixups = []  # (offset in .debug_info, type index)

    def string(self, text):
        if text not in self.string_offsets:
            self.string_offsets[text] = len(self.strings)
            self.strings += text.encode("latin-1") + b"\0"
        return self.string_offsets[text]

    def die(self, tag, children, attributes):
        """Write a DIE: ATTRIBUTES [(attribute, form, value)] (a ref_addr's value: the type index it refers to); its
        offset."""
        at = len(self.info)
        self.info += uleb(self.abbreviations.code(tag, children, [(a, f) for a, f, _ in attributes]))
        for attribute, form, value in attributes:
            if form == "strp":
                self.info += struct.pack("<I", self.string(value))
            elif form in ("addr", "data4", "sec_offset"):
                self.info += struct.pack("<I", value)
            elif form == "data2":
                self.info += struct.pack("<H", value)
            elif form in ("data1", "flag"):
                self.info += bytes([value])
            elif form == "udata":
                self.info += uleb(value)
            elif form == "sdata":
                self.info += sleb(value)
            elif form == "ref_addr":
                self.fixups.append((len(self.info), value))
                self.info += bytes(4)
            elif form == "exprloc":
                self.info += uleb(len(value)) + value
            elif form == "flag_present":
                pass
        return at

    def end_children(self):
        self.info += b"\0"

    def unit_start(self):
        at = len(self.info)
        # (unit length, version 4, abbreviations at 0, address size 4)
        self.info += struct.pack("<IHIB", 0, 4, 0, 4)
        return at

    def unit_end(self, at):
        struct.pack_into("<I", self.info, at, len(self.info) - at - 4)


def write(types_records, units, producer):
    """{section name: bytes} of DWARF for the CodeView 8 TYPES_RECORDS and UNITS: [{name, language (C: 0x1, C++: 0x4),
    files [name], functions [{name, address, size, type, external, variables [{name, type, location (("frame",
    offset), ("register", CodeView register), ("cfa", offset)), argument}], lines [(offset, line)], file,
    first_line}], data [{name, type, address, external}]}]."""
    types = Types(types_records)
    w = Writer()
    offsets = {}

    def ref(index):
        return ("type", "ref_addr", index)

    # (the types' unit)
    unit = w.unit_start()
    w.die(
        "compile_unit", True, [("name", "strp", "<types>"), ("producer", "strp", producer), ("language", "data2", 0x1)]
    )

    def primitive(index):
        key = ("primitive", index)
        if key in offsets:
            return
        mode, base = (index >> 8) & 7, index & 0xFF
        if mode:
            # (a pointer to a primitive type: to void when the base is void)
            attributes = [("byte_size", "data1", 4)]
            if base != 0x03 and base in PRIMITIVES:
                primitive(base)
                attributes.append(("type", "ref_addr", base))
            offsets[key] = w.die("pointer_type", False, attributes)
        elif base in PRIMITIVES:
            name, encoding, size = PRIMITIVES[base]
            offsets[key] = w.die(
                "base_type",
                False,
                [("name", "strp", name), ("encoding", "data1", encoding), ("byte_size", "data1", size)],
            )

    def type_attribute(index):
        """The DW_AT_type of a reference to INDEX, or [] for void and no type."""
        if index < 0x1000:
            if index & 0xFF == 0x03 and not (index >> 8) & 7 or index == 0:
                return []
            primitive(index)
            return [("type", "ref_addr", index)] if ("primitive", index) in offsets else []
        return [("type", "ref_addr", types.resolve(index))]

    for index in sorted(types.records):
        leaf, body = types.records[index]
        if leaf == 0x1002:  # LF_POINTER
            offsets[index] = w.die(
                "pointer_type",
                False,
                [("byte_size", "data1", 4)] + type_attribute(struct.unpack_from("<I", body, 0)[0]),
            )
        elif leaf == 0x1503:  # LF_ARRAY
            element, index_type = struct.unpack_from("<II", body, 0)
            size = numeric(body, 8)[0]
            offsets[index] = w.die("array_type", True, type_attribute(element))
            element_size = types.size(element)
            count = [("count", "udata", size // element_size)] if element_size else []
            w.die("subrange_type", False, type_attribute(index_type) + count)
            w.end_children()
        elif leaf in (0x1504, 0x1505, 0x1506):  # LF_CLASS, LF_STRUCTURE, LF_UNION
            if types.forward(index) and types.resolve(index) != index:
                continue
            tag = "union_type" if leaf == 0x1506 else "structure_type"
            name = types.name(index)
            name_attribute = [] if name == "<unnamed-tag>" else [("name", "strp", name)]
            if types.forward(index):
                offsets[index] = w.die(tag, False, name_attribute + [("declaration", "flag_present", None)])
                continue
            members = types.fields(struct.unpack_from("<I", body, 4)[0])
            offsets[index] = w.die(tag, bool(members), name_attribute + [("byte_size", "udata", types.size(index))])
            if not members:
                continue
            for field, member, kind, offset in members:
                if field == 0x1400:
                    w.die("inheritance", False, type_attribute(kind) + [("data_member_location", "udata", offset)])
                    continue
                record = types.records.get(kind)
                if record and record[0] == 0x1205:  # LF_BITFIELD: the member's type, length, position
                    underlying, length, position = struct.unpack_from("<IBB", record[1], 0)
                    w.die(
                        "member",
                        False,
                        [("name", "strp", member)]
                        + type_attribute(underlying)
                        + [("bit_size", "data1", length), ("data_bit_offset", "udata", offset * 8 + position)],
                    )
                else:
                    w.die(
                        "member",
                        False,
                        [("name", "strp", member)] + type_attribute(kind) + [("data_member_location", "udata", offset)],
                    )
            w.end_children()
        elif leaf == 0x1507:  # LF_ENUM
            if types.forward(index) and types.resolve(index) != index:
                continue
            underlying, fields = struct.unpack_from("<II", body, 4)
            name = types.name(index)
            name_attribute = [] if name == "<unnamed-tag>" else [("name", "strp", name)]
            offsets[index] = w.die(
                "enumeration_type",
                True,
                name_attribute + [("byte_size", "data1", types.size(underlying) or 4)] + type_attribute(underlying),
            )
            for field, member, _, value in types.fields(fields):
                w.die("enumerator", False, [("name", "strp", member), ("const_value", "sdata", value)])
            w.end_children()
        elif leaf == 0x1008:  # LF_PROCEDURE
            result, arguments, variadic = types.arguments(index)
            offsets[index] = w.die(
                "subroutine_type",
                bool(arguments or variadic),
                type_attribute(result) + [("prototyped", "flag_present", None)],
            )
            if not arguments and not variadic:
                continue
            for argument in arguments:
                w.die("formal_parameter", False, type_attribute(argument))
            if variadic:
                w.die("unspecified_parameters", False, [])
            w.end_children()
    w.end_children()
    w.unit_end(unit)

    # (each translation unit: its functions and variables, and its lines)
    def frame(offset):
        return bytes([DW_OP_fbreg]) + sleb(offset)

    for u in units:
        if not u["functions"] and not u["data"]:
            continue
        line_offset = len(w.lines)
        write_lines(w, u)
        unit = w.unit_start()
        w.die(
            "compile_unit",
            True,
            [
                ("name", "strp", u["name"]),
                ("producer", "strp", producer),
                ("language", "data2", u["language"]),
                ("stmt_list", "sec_offset", line_offset),
                ("low_pc", "addr", 0),
                ("comp_dir", "strp", "."),
            ],
        )
        for v in u["data"]:
            w.die(
                "variable",
                False,
                [("name", "strp", v["name"])]
                + type_attribute(v["type"])
                + [
                    ("external", "flag", 1 if v["external"] else 0),
                    ("location", "exprloc", bytes([DW_OP_addr]) + struct.pack("<I", v["address"])),
                ],
            )
        for f in u["functions"]:
            signature = types.arguments(f["type"]) if f["type"] >= 0x1000 else None
            attributes = [
                ("name", "strp", f["name"]),
                ("external", "flag", 1 if f["external"] else 0),
                ("low_pc", "addr", f["address"]),
                ("high_pc", "data4", f["size"]),
                ("frame_base", "exprloc", bytes([DW_OP_call_frame_cfa])),
            ]
            if signature:
                attributes += type_attribute(signature[0]) + [("prototyped", "flag_present", None)]
            if f["file"] in u["files"] and f.get("first_line"):
                attributes += [
                    ("decl_file", "udata", u["files"].index(f["file"]) + 1),
                    ("decl_line", "udata", f["first_line"]),
                ]
            arguments = [v for v in f["variables"] if v["argument"]]
            if not arguments and signature:
                # (no record of the arguments: where the calling convention puts them, unnamed)
                at = 8
                for kind in signature[1]:
                    arguments.append(dict(name=None, type=kind, location=("frame", at), argument=True))
                    at += (types.size(kind) + 3) & ~3 or 4
            children = arguments + [v for v in f["variables"] if not v["argument"]]
            variadic = bool(signature and signature[2])
            w.die("subprogram", bool(children or variadic), attributes)
            if not children and not variadic:
                continue
            for v in children:
                location = v["location"]
                if location[0] == "frame":
                    expression = frame(location[1] - 8)
                elif location[0] == "cfa":
                    expression = frame(location[1])
                elif location[0] == "register" and location[1] in REGISTERS:
                    expression = bytes([DW_OP_reg0 + REGISTERS[location[1]]])
                else:
                    continue
                name = [("name", "strp", v["name"])] if v["name"] else []
                w.die(
                    "formal_parameter" if v["argument"] else "variable",
                    False,
                    name + type_attribute(v["type"]) + [("location", "exprloc", expression)],
                )
            if variadic:
                w.die("unspecified_parameters", False, [])
            w.end_children()
        w.end_children()
        w.unit_end(unit)

    # (the references, now that every type has its place)
    for at, index in w.fixups:
        key = index if index >= 0x1000 else ("primitive", index)
        struct.pack_into("<I", w.info, at, offsets.get(key, 0))
    return {
        ".debug_abbrev": bytes(w.abbreviations.table) + b"\0",
        ".debug_info": bytes(w.info),
        ".debug_line": bytes(w.lines),
        ".debug_str": bytes(w.strings),
    }


def write_lines(w, unit):
    """The unit's line number program (DWARF 4): a sequence per function."""
    program = bytearray()
    for f in unit["functions"]:
        if not f["lines"] or f["file"] not in unit["files"]:
            continue
        program += b"\0\x05\x02" + struct.pack("<I", f["address"])  # DW_LNE_set_address
        program += b"\x04" + uleb(unit["files"].index(f["file"]) + 1)  # DW_LNS_set_file
        offset, line = 0, 1
        for at, number in f["lines"]:
            if at > offset:
                program += b"\x02" + uleb(at - offset)  # DW_LNS_advance_pc
                offset = at
            if number != line:
                program += b"\x03" + sleb(number - line)  # DW_LNS_advance_line
                line = number
            program += b"\x01"  # DW_LNS_copy
        if f["size"] > offset:
            program += b"\x02" + uleb(f["size"] - offset)
        program += b"\0\x01\x01"  # DW_LNE_end_sequence
    files = b"".join(name.encode("latin-1") + b"\0" + b"\0\0\0" for name in unit["files"]) + b"\0"
    header = bytes([1, 1, 1, (-5) & 0xFF, 14, 13]) + bytes([0, 1, 1, 1, 1, 0, 0, 0, 1, 0, 0, 1]) + b"\0" + files
    body = struct.pack("<HI", 4, len(header)) + header + program
    w.lines += struct.pack("<I", len(body)) + body
