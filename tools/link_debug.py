#!/usr/bin/env python3
"""python tools/link_debug.py VERSION OUTPUT: the original executable with debugging information for disassemblers and
debuggers (IDA, Ghidra, Binary Ninja, x64dbg): OUTPUT (build/VERSION/mwcceppc-debug.exe) is the relink's
executable (beside it, mwcceppc.exe, the original) with a CodeView debug directory entry naming OUTPUT's .pdb, which
this writes beside it, and a COFF symbol table; mwcceppc-dwarf.exe, beside it, is the same with the same information
as DWARF in sections of its own (tools/mw/dwarf.py) instead of the PDB. The code and data are the original's, byte for
byte.

Ghidra applies a PDB's types, prototypes, variables and lines but not a function's arguments and locals (unless in
developer mode): the DWARF executable has them for it. IDA, Binary Ninja and debuggers read either.

The PDB (PDB 7.0, tools/mw/pdb.py) has:
- a module per translation unit (config/VERSION/splits.txt), in link order, with its section contributions, so a tool
  shows which unit each function and datum is from;
- a procedure for every function (config/VERSION/functions.json), at the original's address, with its size;
- for each function the sources compile to the original's code (compiled with symbols on: build/VERSION/debug/
  compiled), the compiler's debugging information for it: its type, its locals (the registers and stack slots the
  code keeps them in) and its line numbers. Where the code differs (not decompiled yet, or a unit built -sym off whose
  code symbols on changes), only the function's type and first line: its locals would be another code's;
- every function's arguments where the call puts them (the compiler's virtual frame, the first at 8), named as the
  function's definition in the sources names them: the compiler records only the variables it gives a home (an
  argument it reads once from the stack has no record), and those in the registers it keeps them in;
- the types of every object (merged: one type where the objects have the same one);
- each global and static variable the objects describe, typed, at its address in the original;
- a public symbol for every symbol of config/VERSION/symbols.txt (functions marked as such).

The COFF symbol table repeats the public symbols, statics included, for tools that read no PDB.

The compiler's CodeView is version 5 (it has no DWARF): tools/mw/codeview.py reads it and converts it."""

import hashlib
import os
import re
import struct
import sys
import threading
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import codeview, config, dwarf, formats, pdb, project, verdict
from mw.codeview import Object
from mw.pe import original as original_executable

IMAGE_FILE_LINE_NUMS_STRIPPED, IMAGE_FILE_LOCAL_SYMS_STRIPPED, IMAGE_FILE_DEBUG_STRIPPED = 0x4, 0x8, 0x200


class Image:
    """A PE image's bytes and section headers."""

    def __init__(self, data):
        self.data = bytearray(data)
        self.pe = struct.unpack_from("<I", self.data, 0x3C)[0]
        self.optional = self.pe + 24
        self.base = struct.unpack_from("<I", self.data, self.optional + 28)[0]
        count = struct.unpack_from("<H", self.data, self.pe + 6)[0]
        first = self.optional + struct.unpack_from("<H", self.data, self.pe + 20)[0]
        self.sections = []
        for i in range(count):
            header = first + 40 * i
            name = self.data[header : header + 8].rstrip(b"\0").decode()
            size, address, raw_size, raw = struct.unpack_from("<IIII", self.data, header + 8)
            characteristics = struct.unpack_from("<I", self.data, header + 36)[0]
            self.sections.append(
                dict(
                    name=name,
                    header=header,
                    size=size,
                    address=address,
                    raw_size=raw_size,
                    raw=raw,
                    characteristics=characteristics,
                )
            )

    def segment(self, address):
        """(1-based section number, offset) of the virtual address ADDRESS, or None."""
        rva = address - self.base
        for number, section in enumerate(self.sections, 1):
            if section["address"] <= rva < section["address"] + max(section["size"], 1):
                return number, rva - section["address"]
        return None

    def headers(self):
        """[(name, virtual size, rva, characteristics, the 40 header bytes)]."""
        return [
            (s["name"], s["size"], s["address"], s["characteristics"], bytes(self.data[s["header"] : s["header"] + 40]))
            for s in self.sections
        ]

    def timestamp(self):
        return struct.unpack_from("<I", self.data, self.pe + 8)[0]

    def add_codeview(self, guid, age, pdb_name):
        """Point a CodeView debug directory entry at an RSDS record naming PDB_NAME, appended to the file. The entry
        goes in .rdata's file alignment slack, past its virtual size, which grows by it: nothing moves."""
        rdata = next(s for s in self.sections if s["name"] == ".rdata")
        at = (rdata["size"] + 3) & ~3
        if at + 28 > rdata["raw_size"] or any(self.data[rdata["raw"] + rdata["size"] : rdata["raw"] + at + 28]):
            raise SystemExit("link_debug: no room in .rdata for the debug directory")
        directory = self.optional + 96 + 8 * 6
        if any(self.data[directory : directory + 8]):
            raise SystemExit("link_debug: the executable has a debug directory already")
        record = b"RSDS" + guid + struct.pack("<I", age) + pdb_name.encode() + b"\0"
        self.data += bytes(-len(self.data) % 4)
        pointer = len(self.data)
        self.data += record
        struct.pack_into(
            "<IIHHIIII", self.data, rdata["raw"] + at, 0, self.timestamp(), 0, 0, 2, len(record), 0, pointer
        )
        struct.pack_into("<I", self.data, rdata["header"] + 8, at + 28)
        rdata["size"] = at + 28
        struct.pack_into("<II", self.data, directory, rdata["address"] + at, 28)

    def add_sections(self, sections):
        """Append SECTIONS {name: bytes} as discardable, read-only sections after the image's (as MinGW's debugging
        sections are); a name longer than 8 is the symbol table's (add_symbol_table, after this)."""
        header_end = self.sections[-1]["header"] + 40
        first_raw = min(s["raw"] for s in self.sections if s["raw"])
        if header_end + 40 * len(sections) > first_raw or any(self.data[header_end:first_raw]):
            raise SystemExit("link_debug: no room for more section headers")
        alignment = struct.unpack_from("<I", self.data, self.optional + 32)[0]
        file_alignment = struct.unpack_from("<I", self.data, self.optional + 36)[0]
        address = self.sections[-1]["address"] + self.sections[-1]["size"]
        self.long_names = getattr(self, "long_names", [])
        for name, data in sections.items():
            address = (address + alignment - 1) & ~(alignment - 1)
            self.data += bytes(-len(self.data) % file_alignment)
            raw = len(self.data)
            raw_size = (len(data) + file_alignment - 1) & ~(file_alignment - 1)
            self.data += data + bytes(raw_size - len(data))
            header = self.sections[-1]["header"] + 40
            self.data[header : header + 40] = struct.pack(
                "<8sIIIIIIHHI", b"", len(data), address, raw_size, raw, 0, 0, 0, 0, 0x42100040
            )
            if len(name) <= 8:
                self.data[header : header + 8] = name.encode().ljust(8, b"\0")
            else:
                self.long_names.append((header, name))
            self.sections.append(
                dict(
                    name=name,
                    header=header,
                    size=len(data),
                    address=address,
                    raw_size=raw_size,
                    raw=raw,
                    characteristics=0x42100040,
                )
            )
            address += len(data)
        struct.pack_into("<H", self.data, self.pe + 6, len(self.sections))
        struct.pack_into("<I", self.data, self.optional + 56, (address + alignment - 1) & ~(alignment - 1))

    def add_symbol_table(self, symbols):
        """Append a COFF symbol table of SYMBOLS [(name, address, function, global)]; their number."""
        table, strings, count = bytearray(), bytearray(), 0
        # (the long section names: "/OFFSET" in the string table)
        for header, name in getattr(self, "long_names", []):
            self.data[header : header + 8] = f"/{4 + len(strings)}".encode().ljust(8, b"\0")
            strings += name.encode() + b"\0"
        for name, address, function, is_global in sorted(symbols, key=lambda s: s[1]):
            where = self.segment(address)
            if where is None:
                continue
            encoded = name.encode("latin-1")
            field = encoded.ljust(8, b"\0") if len(encoded) <= 8 else struct.pack("<II", 0, 4 + len(strings))
            if len(encoded) > 8:
                strings += encoded + b"\0"
            # (type 0x20: a function; storage class 2 external, 3 static)
            table += field + struct.pack(
                "<IhHBB", where[1], where[0], 0x20 if function else 0, 2 if is_global else 3, 0
            )
            count += 1
        pointer = len(self.data)
        self.data += table + struct.pack("<I", 4 + len(strings)) + strings
        struct.pack_into("<II", self.data, self.pe + 12, pointer, count)
        characteristics = struct.unpack_from("<H", self.data, self.pe + 22)[0]
        characteristics &= ~(IMAGE_FILE_LINE_NUMS_STRIPPED | IMAGE_FILE_LOCAL_SYMS_STRIPPED | IMAGE_FILE_DEBUG_STRIPPED)
        struct.pack_into("<H", self.data, self.pe + 22, characteristics)
        return count


def proc_record(kind, length, type_index, segment, offset, name):
    """An S_GPROC32 or S_LPROC32 (CodeView 8) with no locals: its end is set once its S_END is written."""
    body = struct.pack("<IIIIIIIIHB", 0, 0, 0, length, 0, length, type_index, offset, segment, 0) + pdb.cstr(name)
    return pdb.symbol(kind, body)


def undecorated(symbol):
    """The C name of a COFF symbol: no leading underscore, no @N of __stdcall (a C++ name stays mangled)."""
    if symbol.startswith("?"):
        return symbol
    name = symbol.removeprefix("_")
    head, _, tail = name.rpartition("@")
    return head if head and tail.isdigit() else name


def exact(text, original, address, size):
    """Whether the function section TEXT is the original's SIZE bytes at ADDRESS (relocated fields aside)."""
    if text["size"] != size:
        return False
    theirs = original.read(address, size)
    mask = bytearray(b"\1" * size)
    for at, _, _ in text["relocs"]:
        mask[at : at + 4] = bytes(4)
    return all(not m or a == b for m, a, b in zip(mask, text["data"], theirs))


INCLUDE_DIRECTORIES = {}  # source: [directory] (config/sources.json), for parameter_names


def parameter_names(path, name, cache={}, seen=None, unit=None):
    """The names of the parameters of the function NAME as its definition in the source PATH (or a file it includes
    with "...") declares them ([] for none, None for an unnamed one), or None when the definition is not found or not
    understood."""
    seen = set() if seen is None else seen
    unit = unit or path
    if path in seen:
        return None
    seen.add(path)
    found = definition_parameters(path, name, cache)
    if found is not None:
        return found
    for included in re.findall(
        r'^\s*#\s*include\s+"([^"]+)"',
        Path(path).read_text(encoding="latin-1") if Path(path).exists() else "",
        re.MULTILINE,
    ):
        for base in [Path(path).parent] + [Path(d) for d in INCLUDE_DIRECTORIES.get(unit, ["include"])]:
            if (base / included).exists():
                found = parameter_names(str(base / included), name, cache, seen, unit)
                if found is not None:
                    return found
                break
    return None


def definition_parameters(path, name, cache):
    """parameter_names in PATH alone."""
    if path not in cache:
        try:
            text = Path(path).read_text(encoding="latin-1")
        except OSError:
            text = ""
        # (comments and preprocessor lines out, strings emptied: what is left is declarations)
        text = re.sub(r"/\*.*?\*/|//[^\n]*", " ", text, flags=re.DOTALL)
        text = re.sub(r'"(\\.|[^"\\])*"', '""', text)
        cache[path] = text
    text = cache[path]
    for match in re.finditer(r"(?<![\w.>])" + re.escape(name.split("::")[-1]) + r"\s*\(", text):
        # (its parameter list, then a body: a definition)
        depth, at = 1, match.end()
        while at < len(text) and depth:
            depth += {"(": 1, ")": -1}.get(text[at], 0)
            at += 1
        if depth or not re.match(r"\s*\{", text[at:]):
            continue
        inside = text[match.end() : at - 1].strip()
        if inside in ("", "void"):
            return []
        names, depth, part = [], 0, ""
        for c in inside + ",":
            if c == "," and depth == 0:
                part = part.strip()
                if part == "...":
                    break
                pointer = re.search(r"\(\s*\*\s*(\w+)\s*\)", part)  # (a function pointer: (*name)(...))
                if pointer:
                    names.append(pointer.group(1))
                else:
                    words = re.findall(r"[A-Za-z_]\w*", re.sub(r"\[[^\]]*\]", "", part))
                    keywords = {
                        "const",
                        "volatile",
                        "register",
                        "struct",
                        "union",
                        "enum",
                        "unsigned",
                        "signed",
                        "int",
                        "char",
                        "short",
                        "long",
                        "float",
                        "double",
                        "void",
                    }
                    names.append(words[-1] if len(words) >= 2 and words[-1] not in keywords else None)
                part = ""
                continue
            depth += {"(": 1, "[": 1, ")": -1, "]": -1}.get(c, 0)
            part += c
        return names
    return None


def source_path(obj):
    """The repository path of the source an object was compiled from (its .file)."""
    if obj is None:
        return None
    name = (obj.source_file() or "").replace("\\", "/")
    for marker in ("/src/", "/include/", "/lib/"):
        if marker in name:
            return name[name.index(marker) + 1 :]
    return name or None


def add_arguments(module, records, obj, types, source, name):
    """Write the arguments of the function whose compiler records are RECORDS into MODULE, as S_BPREL32 records where
    the call puts them (the compiler's virtual frame: the first at 8), named as its definition in SOURCE names them
    (else as the compiler's records of them, in order and by type); [(offset, type, name or None)]."""
    slots = codeview.parameter_slots(obj.types(), struct.unpack_from("<I", records[0][1], 24)[0]) or []
    names = parameter_names(source, name) if source else None
    if names is None or len(names) != len(slots):
        # (no definition to read: the compiler's records, the first ones of each argument's type, in order)
        names, k = [None] * len(slots), 0
        for record_kind, record_body in records[1:]:
            if record_kind not in (0x1001, 0x1006, 0x100D) or k >= len(slots):
                break
            type_at, name_at = {0x1001: (0, 6), 0x1006: (4, 8), 0x100D: (4, 10)}[record_kind]
            if struct.unpack_from("<I", record_body, type_at)[0] != slots[k][1]:
                break
            names[k] = codeview.pstring(record_body, name_at)[0]
            k += 1
    out = []
    for (offset, kind), argument in zip(slots, names):
        kind = types(kind)
        if argument:
            module.add(pdb.symbol(0x110B, struct.pack("<iI", offset, kind) + pdb.cstr(argument)))
        out.append((offset, kind, argument))
    return out


def build(version, output):
    original = original_executable(version)
    image = Image(Path(output).with_name("mwcceppc.exe").read_bytes())
    units = [
        (u.name, [(r.section, r.start, r.end, dict(r.options)) for r in u.ranges])
        for u in formats.read_splits(f"config/{version}/splits.txt").units
    ]
    symbols = [
        (s.name, s.section, s.address, dict(s.options)) for s in formats.read_symbols(f"config/{version}/symbols.txt")
    ]
    for source, settings in config.sources().items():
        INCLUDE_DIRECTORIES[source] = [*settings.include_dirs, "include", *settings.system_include_dirs, "include/libc"]
    # (each function row, by its linker name: C++ names are mangled, C names take an underscore)
    rows = sorted(
        (
            {
                "address": r.address,
                "size": r.size,
                "source": r.source,
                "symbol": r.symbol if r.symbol.startswith("?") else "_" + r.symbol,
            }
            for r in verdict.rows(version)
        ),
        key=lambda r: r["address"],
    )

    # (the objects compiled with symbols on, by source)
    root = Path(f"build/{version}/debug/compiled")
    wanted = {unit for unit, _ in units} | {row.get("source") for row in rows}
    objects = {
        path.relative_to(root).as_posix()[:-4]: Object(path)
        for path in sorted(root.rglob("*.obj"))
        if path.relative_to(root).as_posix()[:-4] in wanted
    }

    # (types: every object's, merged; deep, so on a thread with a deep stack)
    merger = codeview.TypeMerger()
    for source, obj in objects.items():
        merger.add_object(source, obj.types())
    pdb_file = pdb.PDB(b"", 1, image.timestamp(), image.headers())
    failure = []

    def merge():
        try:
            merger.merge(pdb_file.types)
        except BaseException as e:
            failure.append(e)

    threading.stack_size(512 * 1024 * 1024)
    thread = threading.Thread(target=merge)
    thread.start()
    thread.join()
    if failure:
        raise failure[0]

    # (modules: the units, in link order, with their contributions)
    ranges, modules = [], []
    for index, (unit, sections) in enumerate(units):
        object_name = (
            f"build/{version}/debug/compiled/{unit}.obj" if unit in objects else f"build/{version}/obj/{unit}.obj"
        )
        module = pdb_file.module(unit, object_name)
        modules.append(module)
        for section, start, end, _ in sections:
            where = image.segment(start)
            if where is None or end <= start:
                continue
            module.contributions.append(
                (where[0], where[1], end - start, image.sections[where[0] - 1]["characteristics"])
            )
            ranges.append((start, end, index))
        # (what the unit was compiled from, and as what)
        module.add(pdb.symbol(0x1101, struct.pack("<I", 0) + pdb.cstr(object_name)))
        language = 1 if unit.endswith((".cpp", ".cp")) else 0
        module.add(
            pdb.symbol(
                0x113C,
                struct.pack("<IH8H", language, 3, 2, 4, 7, 0, 2, 4, 7, 0)
                + pdb.cstr("Metrowerks CodeWarrior C/C++ x86 V2.4"),
            )
        )
    ranges.sort()
    starts = [r[0] for r in ranges]

    def module_at(address):
        i = bisect_right(starts, address) - 1
        return ranges[i][2] if i >= 0 and ranges[i][0] <= address < ranges[i][1] else None

    # (functions: every row, at its address, in the unit whose range it is in)
    functions = {source: {f["symbol"]["name"]: f for f in obj.functions()} for source, obj in objects.items()}
    counts = defaultdict(int)
    model = [
        dict(
            name=unit,
            language=4 if unit.endswith((".cpp", ".cp")) else 1,
            files=modules[i].files,
            functions=[],
            data=[],
        )
        for i, (unit, _) in enumerate(units)
    ]
    for row in rows:
        index = module_at(row["address"])
        where = image.segment(row["address"])
        if index is None or where is None:
            counts["outside"] += 1
            continue
        module, (segment, offset) = modules[index], where
        source = row.get("source")
        types = lambda t, source=source: merger.index(source, t)
        # (a name the inventory made unique by its address is the object's plain name)
        symbol = row["symbol"]
        if not symbol.startswith("?") and "__" in symbol and symbol.rsplit("__", 1)[1] == f"{row['address']:08x}":
            symbol = symbol.rsplit("__", 1)[0]
        function = functions.get(source, {}).get(symbol)
        records = list(codeview.records(function["debug"]["data"], 0)) if function and function["debug"] else []
        if not records or records[0][0] not in (0x100A, 0x100B):
            records = []
        name, type_index = undecorated(symbol), 0
        kind = pdb.S_LPROC32 if function and function["symbol"]["storage"] == 3 else pdb.S_GPROC32
        if records:
            kind, body = codeview.convert_symbol(records[0][0], records[0][1], types)
            type_index = struct.unpack_from("<I", body, 24)[0]
            name = codeview.cstring(body, 35)[0]
        file = source_path(objects.get(source))
        if records and exact(function["text"], original, row["address"], row["size"]):
            # (the compiler's record of this very code: the procedure, then its arguments and locals)
            body = bytearray(body)
            struct.pack_into("<III", body, 0, 0, 0, 0)
            struct.pack_into("<I", body, 12, row["size"])
            struct.pack_into("<IH", body, 28, offset, segment)
            at = module.add(pdb.symbol(kind, bytes(body)))
            # (the arguments come first, in order; one the code keeps in a register is given its home on the stack,
            # where the compiler's own S_BPREL32 has the others: a virtual frame, the first argument at 8)
            arguments = add_arguments(module, records, objects[source], types, source, name)
            variables = [dict(name=n, type=t, location=("frame", o), argument=True) for o, t, n in arguments if n]
            for record_kind, record_body in records[1:]:
                if record_kind == 0x0006:
                    break
                converted = codeview.convert_symbol(record_kind, record_body, types)
                if not converted:
                    continue
                kind8, body8 = converted
                if kind8 in (0x1106, 0x110B, 0x1111) and codeview.cstring(
                    body8, {0x1106: 6, 0x110B: 8, 0x1111: 10}[kind8]
                )[0] in {n for _, _, n in arguments if n}:
                    continue  # (an argument: written where the call puts it)
                module.add(pdb.symbol(*converted))
                # (the variable, for DWARF: where it is)
                kind8, body8 = converted
                if kind8 == 0x110B:  # S_BPREL32: offset, type, name
                    offset_of, type_of = struct.unpack_from("<iI", body8, 0)
                    location, name_of = ("frame", offset_of), codeview.cstring(body8, 8)[0]
                elif kind8 == 0x1106:  # S_REGISTER: type, register, name
                    type_of, register = struct.unpack_from("<IH", body8, 0)
                    location, name_of = ("register", register), codeview.cstring(body8, 6)[0]
                elif kind8 == 0x1111:  # S_REGREL32: offset, type, register, name
                    offset_of, type_of, register = struct.unpack_from("<iIH", body8, 0)
                    name_of = codeview.cstring(body8, 10)[0]
                    # (EBP: the frame; ESP: from a stack pointer the record does not say, so nowhere for DWARF)
                    location = ("frame", offset_of) if register == 22 else None
                else:
                    continue
                if location:
                    variables.append(dict(name=name_of, type=type_of, location=location, argument=False))
            lines = function["lines"]
            counts["full"] += 1
        else:
            at = module.add(proc_record(kind, row["size"], type_index, segment, offset, name))
            lines = [(0, function["first_line"])] if function and function["first_line"] else []
            # (its arguments: where the call puts them, named as the source does; not its locals: another code's)
            arguments = add_arguments(module, records, objects[source], types, source, name) if records else []
            variables = [dict(name=n, type=t, location=("frame", o), argument=True) for o, t, n in arguments if n]
            counts["typed" if type_index else "bare"] += 1
        model[index]["functions"].append(
            dict(
                name=name,
                address=row["address"],
                size=row["size"],
                type=type_index,
                external=kind == pdb.S_GPROC32,
                variables=variables,
                lines=lines if lines and file else [],
                file=file,
                first_line=function["first_line"] if function else None,
            )
        )
        module.patch(at, 4, module.add(pdb.symbol(pdb.S_END, b"")))
        if lines and file:
            if file not in module.files:
                module.files.append(file)
            module.lines.append((segment, offset, row["size"], file, lines))
        reference = pdb.S_LPROCREF if kind == pdb.S_LPROC32 else pdb.S_PROCREF
        pdb_file.globals.append(
            (name, pdb.symbol(reference, struct.pack("<IIH", 0, at, index + 1) + pdb.cstr(name)), index, at)
        )

    # (data: each global and static the objects describe, at its address: the defining object's record, else the
    # first declaration's)
    by_name = defaultdict(list)
    for name, section, address, options in symbols:
        by_name[name].append(address)
    data = {}
    for source, obj in objects.items():
        for at, kind, body, section in obj.object_symbols():
            if kind not in (0x1007, 0x1008):
                continue
            target = next(
                (obj.symbols[i] for o, i, k in section["relocs"] if o == at + 8 and k == codeview.REL_SECREL), None
            )
            if target is None:
                continue
            candidates = by_name.get(target["name"], [])
            if target["storage"] == 3:
                candidates = [a for a in candidates if module_at(a) is not None and units[module_at(a)][0] == source]
            if len(candidates) != 1:
                continue
            address = candidates[0]
            where = image.segment(address)
            defined = target["section"] > 0
            if where is None or address in data and (data[address][3] or not defined):
                continue
            kind8, body8 = codeview.convert_symbol(kind, body, lambda t, source=source: merger.index(source, t))
            body8 = bytearray(body8)
            struct.pack_into("<IH", body8, 4, where[1], where[0])
            data[address] = (kind8, bytes(body8), codeview.cstring(body8, 10)[0], defined)
    for address, (kind, body, name, _) in sorted(data.items()):
        record = pdb.symbol(kind, body)
        index = module_at(address)
        if kind == pdb.S_LDATA32 and index is not None:
            modules[index].add(record)
        pdb_file.globals.append((name, record, None, 0))
        if index is not None:
            model[index]["data"].append(
                dict(
                    name=name,
                    type=struct.unpack_from("<I", body, 0)[0],
                    address=address,
                    external=kind == pdb.S_GDATA32,
                )
            )
    counts["data"] = len(data)

    # (publics: every symbol)
    for name, section, address, options in symbols:
        where = image.segment(address)
        if where:
            pdb_file.publics.append((name, where[0], where[1], options.get("type") == "function"))

    # (the PDB's identity: of the executable and the types)
    guid = hashlib.sha1(bytes(image.data) + b"".join(pdb_file.types.records)).digest()[:16]
    pdb_file.guid = guid
    pdb_path = Path(output).with_suffix(".pdb")
    pdb_file.write(pdb_path)
    image.add_codeview(guid, 1, pdb_path.name)
    count = image.add_symbol_table(
        [
            (name, address, options.get("type") == "function", options.get("scope") != "local")
            for name, _, address, options in symbols
        ]
    )
    Path(output).write_bytes(bytes(image.data))

    # (the same, as DWARF in the executable itself: mwcceppc-dwarf.exe)
    sections = dwarf.write(pdb_file.types.records, model, "Metrowerks CodeWarrior C/C++ x86 V2.4")
    image = Image(Path(output).with_name("mwcceppc.exe").read_bytes())
    image.add_sections(sections)
    image.add_symbol_table(
        [
            (name, address, options.get("type") == "function", options.get("scope") != "local")
            for name, _, address, options in symbols
        ]
    )
    Path(output).with_name("mwcceppc-dwarf.exe").write_bytes(bytes(image.data))
    return counts, count, len(pdb_file.types.records), pdb_path, merger.variants


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    if len(sys.argv) != 3:
        sys.exit(__doc__.split("\n\n")[0])
    version, output = sys.argv[1], project.relative(Path(sys.argv[2]).absolute())
    # (the configuration and the sources are read by paths relative to the root)
    os.chdir(project.ROOT)
    try:
        counts, symbol_count, type_count, pdb_path, variants = build(project.version(version, original=True), output)
    except project.Error as e:
        sys.exit(f"error: {e}")
    report = (
        f"{output} (and {pdb_path.name}; {Path(output).with_name('mwcceppc-dwarf.exe').name}, DWARF): the "
        f"original, with {counts['full']} functions with their arguments, locals and lines, {counts['typed']} "
        f"with their prototype, arguments and first line (their code is not the sources' yet), "
        f"{counts['bare']} with no source; {counts['data']} variables; {type_count} types; "
        f"and a COFF symbol table of {symbol_count} symbols"
    )
    lines = [report]
    if variants:
        # (a header inconsistency, or two types of one name: the units disagree on the type's layout)
        lines += [
            "",
            f"types the units define differently ({len(variants)}): the name is the most units' definition, "
            "NAME$2... the others'",
        ]
        for name, found in sorted(variants.items()):
            for variant, units in found.items():
                shown = [u.rsplit("/", 1)[1] for u in units]
                more = f" (+{len(shown) - 8})" if len(shown) > 8 else ""
                lines.append(f"  {variant}: {', '.join(shown[:8])}{more}")
    Path(output).with_suffix(".txt").write_text("\n".join(lines) + "\n")
    print(report)


if __name__ == "__main__":
    main()
