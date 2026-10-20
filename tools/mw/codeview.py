"""CodeView as the CodeWarrior x86 compiler writes it into its COFF objects (with -sym on), read for tools/mw/pdb.py.

An object compiled with symbols on has:
- one .debug$T: signature 2 (CV_SIGNATURE_C11), then CodeView 5 type records (the 32-bit type index leaves, names
  length-prefixed: the _ST forms), indices from 0x1000;
- an object-wide .debug$S: signature 2, S_COMPILE, then the object's data (S_GDATA32_ST, S_LDATA32_ST), typedefs
  (S_UDT_ST) and constants;
- for each function, its own COMDAT .debug$S (associative to the function's .text, no signature): S_GPROC32_ST or
  S_LPROC32_ST (with SECREL and SECTION relocations to the function), its arguments and locals (S_REGISTER_ST,
  S_BPREL32_ST, S_REGREL32_ST), blocks and labels, S_END;
- COFF line numbers on each function's .text: an entry naming the function's symbol, then (offset, line) with the line
  relative to the function's .bf (its first line), as the PE/COFF specification has them.

python tools/mw/codeview.py OBJECT...: what the objects hold, by record kind."""

import struct
import sys
from collections import Counter
from pathlib import Path

# relocation types (i386)
REL_DIR32, REL_SECTION, REL_SECREL = 6, 10, 11


def pstring(data, at):
    """(the length-prefixed string at AT, the offset after it)."""
    length = data[at]
    return data[at + 1 : at + 1 + length].decode("latin-1"), at + 1 + length


def cstring(data, at):
    """(the NUL-terminated string at AT, the offset after it)."""
    end = data.index(b"\0", at)
    return data[at:end].decode("latin-1"), end + 1


class Object:
    """A COFF object: its sections [{number, name, flags, data, relocs [(offset, symbol index, type)], lines [(address
    or symbol index, line)]}] and its symbols {index: {name, value, section, type, storage, aux [bytes]}}."""

    def __init__(self, path):
        self.path = Path(path)
        data = self.path.read_bytes()
        _, count, _, symbol_table, symbol_count, optional, _ = struct.unpack_from("<HHIIIHH", data)
        strings = symbol_table + 18 * symbol_count

        def name_of(raw):
            if raw[:4] == b"\0\0\0\0":
                return cstring(data, strings + struct.unpack_from("<I", raw, 4)[0])[0]
            return raw.rstrip(b"\0").decode("latin-1")

        self.sections = []
        for i in range(count):
            header = 20 + optional + 40 * i
            name = data[header : header + 8].rstrip(b"\0").decode("latin-1")
            if name.startswith("/"):
                name = cstring(data, strings + int(name[1:]))[0]
            size, raw, relocations, lines, relocation_count, line_count, flags = struct.unpack_from(
                "<IIIIHHI", data, header + 16
            )
            self.sections.append(
                dict(
                    number=i + 1,
                    name=name,
                    flags=flags,
                    size=size,
                    data=data[raw : raw + size] if raw else b"",
                    relocs=[struct.unpack_from("<IIH", data, relocations + 10 * j) for j in range(relocation_count)],
                    lines=[struct.unpack_from("<IH", data, lines + 6 * j) for j in range(line_count)],
                )
            )
        self.symbols, i = {}, 0
        while i < symbol_count:
            at = symbol_table + 18 * i
            value, section, kind, storage, aux = struct.unpack_from("<IhHBB", data, at + 8)
            self.symbols[i] = dict(
                index=i,
                name=name_of(data[at : at + 8]),
                value=value,
                section=section,
                type=kind,
                storage=storage,
                aux=[data[at + 18 * (k + 1) : at + 18 * (k + 2)] for k in range(aux)],
            )
            i += 1 + aux

    def section(self, number):
        return self.sections[number - 1]

    def source_file(self):
        """The .file symbol's file name."""
        for symbol in self.symbols.values():
            if symbol["name"] == ".file":
                return b"".join(symbol["aux"]).rstrip(b"\0").decode("latin-1")
        return None

    def types(self):
        """The .debug$T's records: [(leaf, bytes after the leaf)], the first being type index 0x1000."""
        for section in self.sections:
            if section["name"] == ".debug$T" and section["data"][:4] == b"\2\0\0\0":
                return list(records(section["data"], 4))
        return []

    def functions(self):
        """Each function of the object: {symbol, text (section), debug (its .debug$S section or None), first line
        (its .bf's), lines [(offset, line)] absolute}."""
        by_section = {}
        for symbol in self.symbols.values():
            if symbol["type"] & 0x20 and symbol["section"] > 0:
                by_section.setdefault(symbol["section"], []).append(symbol)
        # (each function's .debug$S: the COMDAT associated to its .text: the section symbol's aux names it)
        associated = {}
        for symbol in self.symbols.values():
            if (
                symbol["storage"] == 3
                and symbol["aux"]
                and symbol["section"] > 0
                and self.section(symbol["section"])["name"] == ".debug$S"
            ):
                size, relocs, lines, checksum, number, selection = struct.unpack_from("<IHHIHB", symbol["aux"][0])
                if selection == 5:  # IMAGE_COMDAT_SELECT_ASSOCIATIVE
                    associated[number] = symbol["section"]
        order = sorted(self.symbols)
        position = {index: k for k, index in enumerate(order)}
        out = []
        for number, symbols in by_section.items():
            text = self.section(number)
            for symbol in symbols:
                # (its .bf, just after it: the function's first line)
                first = None
                for index in order[position[symbol["index"]] + 1 : position[symbol["index"]] + 4]:
                    s = self.symbols[index]
                    if s["name"] == ".bf" and s["aux"]:
                        first = struct.unpack_from("<H", s["aux"][0], 4)[0]
                        break
                lines, own = [], False
                for address, line in text["lines"]:
                    if line == 0:
                        own = address == symbol["index"]
                    elif own and first is not None:
                        lines.append((address - symbol["value"], first + line - 1))
                out.append(
                    dict(
                        symbol=symbol,
                        text=text,
                        first_line=first,
                        lines=lines,
                        debug=self.section(associated[number]) if number in associated else None,
                    )
                )
        return out

    def object_symbols(self):
        """The object-wide .debug$S's records [(offset, kind, bytes after the kind, section)]."""
        out = []
        for section in self.sections:
            if section["name"] == ".debug$S" and section["data"][:4] == b"\2\0\0\0":
                out += [(offset, kind, body, section) for offset, kind, body in records(section["data"], 4, True)]
        return out


def records(data, at, offsets=False):
    """The records from AT: (leaf or kind, bytes after it) (with OFFSETS, (offset, kind, bytes))."""
    while at + 4 <= len(data):
        length, kind = struct.unpack_from("<HH", data, at)
        if length < 2:
            break
        body = data[at + 4 : at + 2 + length]
        yield (at, kind, body) if offsets else (kind, body)
        at += 2 + length


# --- CodeView 5 (the compiler's) to CodeView 8 (PDB 7.0) -------------------------------------------------------------
#
# The compiler's records are the 32-bit type index forms of CodeView 5 with length-prefixed names (the _ST forms); PDB
# 7.0 has the same records with NUL-terminated names, under other leaf and kind numbers. A length-prefixed name and a
# NUL-terminated one are the same size, so a record converts in place: its leaf or kind, its names, and the size of a
# 32-bit near pointer, which CodeView 8 keeps in the pointer's attributes and CodeView 5 leaves 0.

TYPE_LEAVES = {0x1003: 0x1503, 0x1004: 0x1504, 0x1005: 0x1505, 0x1006: 0x1506, 0x1007: 0x1507}
FIELD_LEAVES = {0x1405: 0x150D, 0x0403: 0x1502}
UDT_LEAVES = (0x1504, 0x1505, 0x1506, 0x1507)
SYMBOL_KINDS = {
    0x1001: 0x1106,
    0x1006: 0x110B,
    0x100D: 0x1111,
    0x1007: 0x110C,
    0x1008: 0x110D,
    0x100A: 0x110F,
    0x100B: 0x1110,
    0x0006: 0x0006,
}


def skip_numeric(data, at):
    value = struct.unpack_from("<H", data, at)[0]
    if value < 0x8000:
        return at + 2
    return (
        at
        + 2
        + {0x8000: 1, 0x8001: 2, 0x8002: 2, 0x8003: 4, 0x8004: 4, 0x8005: 4, 0x8006: 8, 0x8009: 8, 0x800A: 8}[value]
    )


def name_to_cstring(body, at):
    """BODY with the length-prefixed name at AT made NUL-terminated (the same size); the offset after it."""
    length = body[at]
    body[at : at + 1 + length] = body[at + 1 : at + 1 + length] + b"\0"
    return at + 1 + length


def primitive(index):
    """A primitive type index as PDBs have it: the compiler writes plain char as T_CHAR (signed char), compilers
    writing PDBs as T_RCHAR (char), in every pointer mode."""
    return (index & ~0xFF) | 0x70 if index < 0x1000 and index & 0xFF == 0x10 else index


def stack_size(records, index):
    """The bytes the argument of type INDEX takes on the stack (4-aligned), from an object's CodeView 5 RECORDS."""
    if index < 0x1000:
        if (index >> 8) & 7:
            return 4
        size = {0x13: 8, 0x23: 8, 0x76: 8, 0x77: 8, 0x41: 8, 0x42: 8}.get(index & 0xFF, 4)
        return size
    leaf, body = records[index - 0x1000]
    if leaf in (0x1004, 0x1005):
        return (numeric_value(body, 16) + 3) & ~3
    if leaf == 0x1006:
        return (numeric_value(body, 8) + 3) & ~3
    return 4


def parameter_slots(records, procedure):
    """[(the virtual frame offset (as the compiler's S_BPREL32: the first argument at 8), the type)] of the arguments
    of the procedure type PROCEDURE (an object's type index), or None."""
    if procedure < 0x1000 or records[procedure - 0x1000][0] != 0x1008:
        return None
    arguments = struct.unpack_from("<I", records[procedure - 0x1000][1], 8)[0]
    if arguments < 0x1000 or records[arguments - 0x1000][0] != 0x1201:
        return None
    body = records[arguments - 0x1000][1]
    offsets, at = [], 8
    for k in range(struct.unpack_from("<I", body, 0)[0]):
        index = struct.unpack_from("<I", body, 4 + 4 * k)[0]
        if index == 0:  # (... : no slot)
            break
        offsets.append((at, index))
        at += stack_size(records, index)
    return offsets


def convert_type(leaf, data):
    """(the CodeView 8 leaf, its body, the offsets of the type indices in it) of a CodeView 5 type record. The body
    ends at its last field: the record's LF_PAD bytes are the writer's to add (a field list keeps those between its
    members, which align them)."""
    body = bytearray(data)
    refs = []
    if leaf == 0x1002:  # LF_POINTER: referent, attributes (and a pointer to member's class)
        refs = [0]
        attributes = struct.unpack_from("<I", body, 4)[0]
        if attributes & 0x1F == 0x0A and not (attributes >> 13) & 0x3F:
            struct.pack_into("<I", body, 4, attributes | 4 << 13)
        end = 8
        if (attributes >> 5) & 7 in (2, 3):
            refs.append(8)
            end = 14
    elif leaf == 0x1003:  # LF_ARRAY: element, index type, size, name
        refs = [0, 4]
        end = name_to_cstring(body, skip_numeric(body, 8))
    elif leaf in (0x1004, 0x1005):  # LF_CLASS, LF_STRUCTURE: count, property, fields, derived, vshape, size, name
        refs = [4, 8, 12]
        end = name_to_cstring(body, skip_numeric(body, 16))
    elif leaf == 0x1006:  # LF_UNION: count, property, fields, size, name
        refs = [4]
        end = name_to_cstring(body, skip_numeric(body, 8))
    elif leaf == 0x1007:  # LF_ENUM: count, property, underlying type, fields, name
        refs = [4, 8]
        end = name_to_cstring(body, 12)
    elif leaf == 0x1008:  # LF_PROCEDURE: return type, call, attributes, parameter count, argument list
        refs = [0, 8]
        end = 12
    elif leaf == 0x1201:  # LF_ARGLIST
        count = struct.unpack_from("<I", body, 0)[0]
        refs = [4 + 4 * i for i in range(count)]
        end = 4 + 4 * count
    elif leaf == 0x1205:  # LF_BITFIELD: type, length, position
        refs = [0]
        end = 6
    elif leaf == 0x1203:  # LF_FIELDLIST: its members, each 4-aligned with LF_PAD bytes
        at = end = 0
        while at < len(body):
            if body[at] >= 0xF0:
                at += body[at] & 0x0F
                continue
            field = struct.unpack_from("<H", body, at)[0]
            struct.pack_into("<H", body, at, FIELD_LEAVES.get(field, field))
            # (the compiler leaves a C member's access 0, none; PDBs have it public, 3)
            attributes = struct.unpack_from("<H", body, at + 2)[0]
            if not attributes & 3:
                struct.pack_into("<H", body, at + 2, attributes | 3)
            if field == 0x1405:  # LF_MEMBER: attributes, type, offset, name
                refs.append(at + 4)
                at = name_to_cstring(body, skip_numeric(body, at + 8))
            elif field == 0x0403:  # LF_ENUMERATE: attributes, value, name
                at = name_to_cstring(body, skip_numeric(body, at + 4))
            elif field == 0x1400:  # LF_BCLASS: attributes, base class, offset
                refs.append(at + 4)
                at = skip_numeric(body, at + 8)
            else:
                raise ValueError(f"field leaf 0x{field:04x}")
            end = at
    else:
        raise ValueError(f"type leaf 0x{leaf:04x}")
    body = body[:end]
    for at in refs:
        struct.pack_into("<I", body, at, primitive(struct.unpack_from("<I", body, at)[0]))
    leaf = TYPE_LEAVES.get(leaf, leaf)
    if leaf in UDT_LEAVES and body[name_offset(leaf, body)] in (0, ord("@")):
        # (an anonymous structure, union or enum, which the compiler names "" or @enum, @class$N<file>...: named as
        # compilers name them, which readers know)
        body = body[: name_offset(leaf, body)] + b"<unnamed-tag>\0"
    return leaf, bytes(body), refs


def name_offset(leaf, body):
    """Where the name of the class, structure, union or enum LEAF/BODY (CodeView 8) starts."""
    if leaf == 0x1507:
        return 12
    return skip_numeric(body, 8 if leaf == 0x1506 else 16)


def forward_reference(leaf, body):
    """The forward reference record of the class, structure, union or enum LEAF/BODY (CodeView 8)."""
    count, options = struct.unpack_from("<HH", body, 0)
    options |= 0x80
    if leaf == 0x1507:
        return leaf, struct.pack("<HHII", 0, options, struct.unpack_from("<I", body, 4)[0], 0) + bytes(body[12:])
    if leaf == 0x1506:
        return leaf, struct.pack("<HHI", 0, options, 0) + struct.pack("<H", 0) + bytes(body[skip_numeric(body, 8) :])
    return leaf, struct.pack("<HHIII", 0, options, 0, 0, 0) + struct.pack("<H", 0) + bytes(
        body[skip_numeric(body, 16) :]
    )


def field_members(body):
    """(leaf, offset or value, position) of each member of the CodeView 8 field list BODY."""
    at = 0
    while at < len(body):
        if body[at] >= 0xF0:
            at += body[at] & 0x0F
            continue
        field = struct.unpack_from("<H", body, at)[0]
        if field == 0x150D:  # LF_MEMBER: attributes, type, offset, name
            yield field, numeric_value(body, at + 8), at
            at = body.index(0, skip_numeric(body, at + 8)) + 1
        elif field == 0x1502:  # LF_ENUMERATE: attributes, value, name
            yield field, numeric_value(body, at + 4), at
            at = body.index(0, skip_numeric(body, at + 4)) + 1
        elif field == 0x1400:  # LF_BCLASS: attributes, type, offset
            yield field, numeric_value(body, at + 8), at
            at = skip_numeric(body, at + 8)
        else:
            raise ValueError(f"field leaf 0x{field:04x}")


def numeric_value(body, at):
    value = struct.unpack_from("<H", body, at)[0]
    if value < 0x8000:
        return value
    form = {0x8000: "<b", 0x8001: "<h", 0x8002: "<H", 0x8003: "<i", 0x8004: "<I", 0x8009: "<q", 0x800A: "<Q"}[value]
    return struct.unpack_from(form, body, at + 2)[0]


class TypeMerger:
    """The types of many objects as one type stream: the objects' records converted to CodeView 8, and the same type
    written once.

    As compilers write PDB types (MSVC, LLVM), a reference to a named structure, union or enum is to its forward
    reference, one per name, which a reader resolves to the definition by name: a pointer to a structure is then the
    same type in every object, whether the object saw the structure's definition or only its declaration (an
    incomplete type, which this compiler writes as an empty structure: here, the forward reference itself). The
    records that are then the same (the same bytes, referring to the same types) are merged. Anonymous types are
    referred to directly; C types can only refer to themselves through a named one, so nothing refers back."""

    def __init__(self):
        self.leaves, self.bodies, self.refs, self.children = [], [], [], []
        self.bases = {}

    def add_object(self, key, records):
        """Add an object's records (Object.types()) under KEY."""
        base = len(self.leaves)
        self.bases[key] = (base, len(records))
        for leaf, data in records:
            leaf, body, refs = convert_type(leaf, data)
            body = bytearray(body)
            children = []
            for at in refs:
                index = struct.unpack_from("<I", body, at)[0]
                if index >= 0x1000:
                    if index - 0x1000 >= len(records):
                        raise ValueError(f"{key}: type 0x{index:x} out of range")
                    children.append(base + index - 0x1000)
                    struct.pack_into("<I", body, at, 0)
                else:
                    children.append(None)
            self.leaves.append(leaf)
            self.bodies.append(bytes(body))
            self.refs.append(refs)
            self.children.append(children)

    def name(self, i):
        """The name of record I if it is a named class, structure, union or enum, else None."""
        leaf, body = self.leaves[i], self.bodies[i]
        if leaf not in UDT_LEAVES:
            return None
        at = name_offset(leaf, body)
        name = body[at : body.index(0, at)]
        return None if name == b"<unnamed-tag>" else name

    def complete(self, i):
        """Whether the class, structure, union or enum I is a definition (not a declaration: the compiler writes an
        incomplete type as one with no members, and an empty field list)."""
        count, options = struct.unpack_from("<HH", self.bodies[i], 0)
        return not options & 0x80 and count > 0

    def c_aggregates(self):
        """Write the C++ objects' classes as what they are in C: the compiler's C++ writes every aggregate as a class,
        a structure or a union, where C writes a structure or a union. A class with no base class is a structure, or a
        union when its members (two or more, not bit fields) are all at offset 0, as no structure's can be."""
        for i, leaf in enumerate(self.leaves):
            if leaf != 0x1504:
                continue
            fields = self.children[i][0]
            members = list(field_members(self.bodies[fields])) if fields is not None else []
            if any(kind == 0x1400 for kind, _, _ in members):
                continue
            body = self.bodies[i]
            offsets = [offset for kind, offset, _ in members if kind == 0x150D]
            positions = [at for kind, _, at in members if kind == 0x150D]
            bitfields = (
                any(
                    self.children[fields][k] is not None and self.leaves[self.children[fields][k]] == 0x1205
                    for k in range(len(positions))
                )
                if fields is not None
                else False
            )
            if len(offsets) >= 2 and not any(offsets) and not bitfields:
                # (count, property, fields, size, name: no derived list or vshape)
                self.leaves[i] = 0x1506
                self.bodies[i] = body[:8] + body[16:]
                self.refs[i] = [4]
                self.children[i] = self.children[i][:1]
            else:
                self.leaves[i] = 0x1505

    def merge(self, types):
        """Write the merged records into TYPES (pdb.Types); then index(KEY, TYPE INDEX) is a type's index there, and
        self.variants lists the names with more than one definition."""
        self.c_aggregates()
        owner = {}
        for key, (base, count) in self.bases.items():
            for i in range(base, base + count):
                owner[i] = key
        # (a name the objects define differently (the units see different declarations of it): the definition the
        # most objects have keeps the name, and is the one references by name find; the others are NAME$2,
        # NAME$3..., which only the variables and functions of the objects that have them use)
        state = self.partition({})
        renames, self.variants = {}, {}
        for key, found in state["definitions"].items():
            if len(found) < 2:
                continue
            ranked = sorted(found, key=lambda c: (-len(found[c]), min(found[c])))
            name = key[1].decode("latin-1")
            self.variants[name] = {name: sorted({owner[i] for i in found[ranked[0]]})}
            for rank, c in enumerate(ranked[1:], 2):
                for i in found[c]:
                    renames[i] = key[1] + b"$%d" % rank
                self.variants[name][f"{name}${rank}"] = sorted({owner[i] for i in found[c]})
        if renames:
            state = self.partition(renames)
        leaves, bodies, children, refs = state["graph"]
        classes, first, keys = state["classes"], state["first"], state["keys"]
        emitted, active = {}, set()
        sys.setrecursionlimit(max(sys.getrecursionlimit(), 200000))

        def emit(c):
            if c in emitted:
                return emitted[c]
            if c in active:
                raise ValueError("a type refers to itself other than through a named structure")
            active.add(c)
            i = first[c]
            body = bytearray(bodies[i])
            for at, child in zip(refs[i], children[i]):
                if child is not None:
                    struct.pack_into("<I", body, at, emit(classes[child]))
            active.discard(c)
            emitted[c] = types.add(leaves[i], bytes(body))
            return emitted[c]

        for c in sorted(first, key=first.get):
            emit(c)
        # (a declaration's type index: the definition of its name; or its forward reference, if there is none)
        self.resolved = {}
        for i, key in keys.items():
            if self.complete(i):
                self.resolved[i] = emitted[classes[i]]
            elif key in state["primary"]:
                self.resolved[i] = emitted[state["primary"][key]]
            else:
                self.resolved[i] = emitted[classes[state["forward"][key]]]
        self.classes, self.emitted = classes, emitted
        self.graph = (leaves, bodies, children)

    def partition(self, renames):
        """The records with RENAMES {record: name} applied, references to named types made references to the
        forward reference of their name, and merged: {graph, classes, first (a record of each class), keys (each
        named type's name key), forward (each name's forward reference record), definitions {key: {class:
        [records]}}}."""
        count = len(self.leaves)
        leaves, bodies = list(self.leaves), list(self.bodies)
        for i, name in renames.items():
            at = name_offset(leaves[i], bodies[i])
            bodies[i] = bodies[i][:at] + name + b"\0"
        keys, model = {}, {}
        for i in range(count):
            name = self.name(i)
            if name is None:
                continue
            # (C's structure, union and enum tags are one name space; the compiler writes a declaration of any of
            # them as a structure. A renamed variant keeps its name's key: references by name are to the name)
            key = (0, name)
            keys[i] = key
            primary = self.complete(i) and i not in renames
            if key not in model or primary and not (self.complete(model[key]) and model[key] not in renames):
                model[key] = i
        children = [list(c) for c in self.children]
        refs = list(self.refs)
        forward = {}
        for key, i in model.items():
            leaf, body = forward_reference(leaves[i], bodies[i])
            leaves.append(leaf)
            bodies.append(body)
            children.append([])
            refs.append([])
            forward[key] = len(leaves) - 1
        target = {i: forward[key] for i, key in keys.items()}
        for i in range(count):
            children[i] = [None if c is None else target.get(c, c) for c in children[i]]
        live = [i for i in range(len(leaves)) if not (i in keys and not self.complete(i))]
        # (the coarsest partition of the records that agree on their own bytes and on the classes of what they refer
        # to, in order)
        labels = {}
        classes = {
            i: labels.setdefault((leaves[i], bodies[i], tuple(c is None for c in children[i])), len(labels))
            for i in live
        }
        while True:
            signatures = {}
            refined = {
                i: signatures.setdefault(
                    (classes[i], tuple(-1 if c is None else classes[c] for c in children[i])), len(signatures)
                )
                for i in live
            }
            if len(signatures) == len(set(classes.values())):
                break
            classes = refined
        first = {}
        for i in live:
            first.setdefault(classes[i], i)
        definitions, primary = {}, {}
        for i, key in keys.items():
            if self.complete(i):
                definitions.setdefault(key, {}).setdefault(classes[i], []).append(i)
                if i not in renames:
                    primary.setdefault(key, classes[i])
        return dict(
            graph=(leaves, bodies, children, refs),
            classes=classes,
            first=first,
            keys=keys,
            forward=forward,
            definitions=definitions,
            primary=primary,
        )

    def index(self, key, index):
        """The merged type index of KEY's type INDEX (a structure's definition where there is one)."""
        if index < 0x1000:
            return primitive(index)
        base, count = self.bases[key]
        if index - 0x1000 >= count:
            return 0
        node = base + index - 0x1000
        if node in self.resolved:
            return self.resolved[node]
        return self.emitted[self.classes[node]]


def convert_symbol(kind, data, type_index):
    """(the CodeView 8 kind, its body) of a CodeView 5 symbol record, its type indices mapped by TYPE_INDEX; None for
    a record PDB 7.0 has no use for (S_COMPILE, S_SSEARCH, S_RETURN, S_ENDARG)."""
    if kind not in SYMBOL_KINDS:
        return None
    body = bytearray(data)
    if kind == 0x1001:  # S_REGISTER: type, register, name
        struct.pack_into("<I", body, 0, type_index(struct.unpack_from("<I", body, 0)[0]))
        name_to_cstring(body, 6)
    elif kind in (0x1006, 0x100D):  # S_BPREL32: offset, type, name; S_REGREL32: offset, type, register, name
        struct.pack_into("<I", body, 4, type_index(struct.unpack_from("<I", body, 4)[0]))
        name_to_cstring(body, 8 if kind == 0x1006 else 10)
    elif kind in (0x1007, 0x1008):  # S_LDATA32, S_GDATA32: type, offset, segment, name
        struct.pack_into("<I", body, 0, type_index(struct.unpack_from("<I", body, 0)[0]))
        name_to_cstring(body, 10)
    elif kind in (0x100A, 0x100B):  # S_LPROC32, S_GPROC32: parent, end, next, length, debug start and end, type,
        # offset, segment, flags, name
        struct.pack_into("<I", body, 24, type_index(struct.unpack_from("<I", body, 24)[0]))
        # (flag 1: CodeView 5's "frame pointer omitted" is CodeView 8's "frame pointer present", CV_PFLAG_NOFPO)
        body[34] ^= 1
        name_to_cstring(body, 35)
    return SYMBOL_KINDS[kind], body


def main():
    symbol_kinds, type_kinds, field_kinds = Counter(), Counter(), Counter()
    for path in sys.argv[1:]:
        obj = Object(path)
        for kind, body in obj.types():
            type_kinds[kind] += 1
            if kind == 0x1203:  # LF_FIELDLIST: its subrecords
                at = 0
                while at + 2 <= len(body):
                    leaf = struct.unpack_from("<H", body, at)[0]
                    if leaf >= 0xF0:
                        at += 1
                        continue
                    field_kinds[leaf] += 1
                    break
        for _, kind, _, _ in obj.object_symbols():
            symbol_kinds[kind] += 1
        for function in obj.functions():
            if function["debug"]:
                for kind, _ in records(function["debug"]["data"], 0):
                    symbol_kinds[kind] += 1
    print("symbols", {hex(k): v for k, v in sorted(symbol_kinds.items())})
    print("types", {hex(k): v for k, v in sorted(type_kinds.items())})
    print("first field", {hex(k): v for k, v in sorted(field_kinds.items())})


if __name__ == "__main__":
    main()
