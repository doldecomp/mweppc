"""A PDB 7.0 writer (MSF 7.00): what IDA, Ghidra, Binary Ninja and debuggers read for a Windows executable.

The streams are laid out as LLVM's PDB writer (lld-link) lays them out, with its hash functions, so that
llvm-pdbutil can check them: the PDB info stream (the named stream map, /names), TPI and IPI (type records and the
TPI hash stream), DBI (modules, section contributions, the section map, source files, the section headers stream),
each module's symbol stream (symbols, C13 line and file checksum subsections), the global and public symbol hash
streams and the symbol record stream.

Records are given already encoded (CodeView 8: tools/mw/codeview.py converts the compiler's CodeView 5); this module
only lays them out."""

import struct
import zlib

BLOCK = 4096
MSF_MAGIC = b"Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0"

# symbol kinds the layout looks at
S_END, S_GPROC32, S_LPROC32, S_PUB32, S_PROCREF, S_LPROCREF = 0x0006, 0x1110, 0x110F, 0x110E, 0x1125, 0x1127
S_GDATA32, S_LDATA32, S_UDT, S_CONSTANT = 0x110D, 0x110C, 0x1108, 0x1107
DEBUG_S_LINES, DEBUG_S_FILECHKSMS = 0xF2, 0xF4
IPHR_HASH = 4096


def hash_string_v1(data):
    """LLVM's hashStringV1 (the PDB's string hash)."""
    result = 0
    whole = len(data) // 4 * 4
    for i in range(0, whole, 4):
        result ^= struct.unpack_from("<I", data, i)[0]
    rest = data[whole:]
    if len(rest) >= 2:
        result ^= struct.unpack_from("<H", rest, 0)[0]
        rest = rest[2:]
    if rest:
        result ^= rest[0]
    result |= 0x20202020
    result ^= result >> 11
    return (result ^ (result >> 16)) & 0xFFFFFFFF


def hash_buffer_v8(data):
    """LLVM's hashBufferV8: JamCRC from 0, no final inversion."""
    return zlib.crc32(data, 0xFFFFFFFF) ^ 0xFFFFFFFF


def symbol(kind, body):
    """A symbol record: its length, KIND, BODY, zero-padded to 4 bytes."""
    body = bytes(body) + bytes(-(len(body) + 4) % 4)
    return struct.pack("<HH", len(body) + 2, kind) + body


def type_record(leaf, body):
    """A type record: its length, LEAF, BODY, padded to 4 bytes with LF_PAD bytes."""
    padding = -(len(body) + 4) % 4
    body = bytes(body) + bytes(0xF0 | n for n in range(padding, 0, -1))
    return struct.pack("<HH", len(body) + 2, leaf) + body


def cstr(name):
    return name.encode("latin-1") + b"\0"


def numeric(value):
    """A CodeView numeric leaf."""
    if 0 <= value < 0x8000:
        return struct.pack("<H", value)
    if -0x80000000 <= value < 0x80000000:
        return struct.pack("<Hi", 0x8003, value)
    return struct.pack("<HQ", 0x800A, value)


class Types:
    """A type stream (TPI or IPI): records from index 0x1000."""

    def __init__(self):
        self.records = []

    def add(self, leaf, body):
        self.records.append(type_record(leaf, body))
        return 0x1000 + len(self.records) - 1

    def streams(self, hash_stream_index):
        """(the type stream, its hash stream) with the hash stream at HASH_STREAM_INDEX (None: no hash stream)."""
        buckets = 0x3FFFF
        data = b"".join(self.records)
        values, offsets, at, last = [], [], 0, None
        for index, record in enumerate(self.records):
            values.append(record_hash(record) % buckets)
            if last is None or at - last >= 8192:
                offsets.append((0x1000 + index, at))
                last = at
            at += len(record)
        hashes = struct.pack(f"<{len(values)}I", *values)
        index_offsets = b"".join(struct.pack("<II", *pair) for pair in offsets)
        header = struct.pack(
            "<IIIIIHHIIiIiIiI",
            20040203,
            56,
            0x1000,
            0x1000 + len(self.records),
            len(data),
            0xFFFF if hash_stream_index is None else hash_stream_index,
            0xFFFF,
            4,
            buckets,
            0,
            len(hashes),
            len(hashes),
            len(index_offsets),
            len(hashes) + len(index_offsets),
            0,
        )
        return header + data, hashes + index_offsets


def record_hash(record):
    """LLVM's hashTypeRecord: a named, defined class, structure, union or enum by its name, anything else by the
    CRC of its bytes."""
    leaf = struct.unpack_from("<H", record, 2)[0]
    if leaf in (0x1504, 0x1505, 0x1506, 0x1507):
        body = record[4:]
        options = struct.unpack_from("<H", body, 2)[0]
        if leaf == 0x1507:
            at = 12
        else:
            at = 8 if leaf == 0x1506 else 16
            at = skip_numeric(body, at)
        name = body[at : body.index(b"\0", at)]
        forward, scoped, unique = options & 0x80, options & 0x100, options & 0x200
        anonymous = unique and name in (b"<unnamed-tag>", b"__unnamed")
        if not forward and not scoped and not anonymous:
            return hash_string_v1(name)
    return hash_buffer_v8(record)


def skip_numeric(data, at):
    value = struct.unpack_from("<H", data, at)[0]
    if value < 0x8000:
        return at + 2
    return (
        at
        + 2
        + {0x8000: 1, 0x8001: 2, 0x8002: 2, 0x8003: 4, 0x8004: 4, 0x8005: 4, 0x8006: 8, 0x8009: 8, 0x800A: 8}[value]
    )


class Strings:
    """The /names string table: offsets of the strings in it."""

    def __init__(self):
        self.data, self.offsets = bytearray(b"\0"), {"": 0}

    def add(self, string):
        if string not in self.offsets:
            self.offsets[string] = len(self.data)
            self.data += cstr(string)
        return self.offsets[string]

    def stream(self):
        names = [s for s in self.offsets if s]
        count = max(1, len(names) * 2 + 1)
        buckets = [0] * count
        for name in names:
            slot = hash_string_v1(name.encode("latin-1")) % count
            while buckets[slot]:
                slot = (slot + 1) % count
            buckets[slot] = self.offsets[name]
        return (
            struct.pack("<III", 0xEFFEEFFE, 1, len(self.data))
            + bytes(self.data)
            + struct.pack(f"<I{count}I", count, *buckets)
            + struct.pack("<I", len(names))
        )


class Module:
    """A module (translation unit): its symbols (the records, encoded, whose offsets the procedures' parent and end
    fields name), its files and its line subsections."""

    def __init__(self, name, object_name):
        self.name, self.object_name = name, object_name
        self.symbols = bytearray()
        self.files = []  # [file name]
        self.lines = []  # [(segment, offset, code size, file name, [(offset, line)])]
        self.contributions = []  # [(segment, offset, size, characteristics)]

    def offset(self):
        """The offset the next record will have in the module's stream (after its 4-byte signature)."""
        return 4 + len(self.symbols)

    def add(self, record):
        at = self.offset()
        self.symbols += record
        return at

    def patch(self, at, field, value):
        """Set the u32 FIELD (byte offset after the record's kind) of the record at AT."""
        struct.pack_into("<I", self.symbols, at - 4 + 4 + field, value)

    def stream(self, strings):
        """(the module's stream, its symbol byte size, its C13 byte size)."""
        checksums, positions = bytearray(), {}
        for name in self.files:
            positions[name] = len(checksums)
            checksums += struct.pack("<IBBH", strings.add(name), 0, 0, 0)
        c13 = bytearray()
        if self.files:
            c13 += struct.pack("<II", DEBUG_S_FILECHKSMS, len(checksums)) + checksums
        for segment, offset, size, file, lines in self.lines:
            block = struct.pack("<III", positions[file], len(lines), 12 + 8 * len(lines))
            block += b"".join(struct.pack("<II", at, (line & 0xFFFFFF) | 0x80000000) for at, line in lines)
            body = struct.pack("<IHHI", offset, segment, 0, size) + block
            c13 += struct.pack("<II", DEBUG_S_LINES, len(body)) + body + bytes(-len(body) % 4)
        symbols = struct.pack("<I", 4) + bytes(self.symbols)
        return symbols + bytes(c13) + struct.pack("<I", 0), len(symbols), len(c13)


def gsi_hash(entries):
    """The GSI hash table of ENTRIES [(name, offset in the symbol record stream)]."""
    buckets = [[] for _ in range(IPHR_HASH + 1)]
    for name, offset in entries:
        buckets[hash_string_v1(name.encode("latin-1")) % IPHR_HASH].append((name, offset))
    records, bitmap, starts = bytearray(), [0] * ((IPHR_HASH + 32) // 32), []
    count = 0
    for i, bucket in enumerate(buckets):
        if not bucket:
            continue
        bitmap[i // 32] |= 1 << (i % 32)
        starts.append(count * 12)
        for name, offset in sorted(bucket, key=lambda e: gsi_key(e[0])):
            records += struct.pack("<II", offset + 1, 1)
            count += 1
    tail = struct.pack(f"<{len(bitmap)}I", *bitmap) + struct.pack(f"<{len(starts)}I", *starts)
    return struct.pack("<IIII", 0xFFFFFFFF, 0xEFFE0000 + 19990810, len(records), len(tail)) + bytes(records) + tail


def gsi_key(name):
    """LLVM's gsiRecordCmp order: by length, then case-insensitively."""
    return len(name), name.lower()


class PDB:
    """The PDB of an executable: modules, publics and globals, types, its section headers."""

    def __init__(self, guid, age, signature, section_headers):
        self.guid, self.age, self.signature = guid, age, signature
        self.section_headers = section_headers  # [(name, virtual size, virtual address, characteristics, raw 40 bytes)]
        self.types, self.ids = Types(), Types()
        self.modules = []
        self.publics = []  # [(name, segment, offset, function)]
        self.globals = []  # [(name, record bytes, module index or None, offset in module)]

    def module(self, name, object_name):
        module = Module(name, object_name)
        self.modules.append(module)
        return module

    def write(self, path):
        strings = Strings()
        for module in self.modules:
            for name in module.files:
                strings.add(name)
        streams = [b"", None, None, None, None]  # 0 old directory, 1 info, 2 TPI, 3 DBI, 4 IPI

        def add(data):
            streams.append(data)
            return len(streams) - 1

        # (types and their hash streams)
        tpi_hash = len(streams)
        streams.append(None)
        streams[2], streams[tpi_hash] = self.types.streams(tpi_hash)
        ipi_hash = len(streams)
        streams.append(None)
        streams[4], streams[ipi_hash] = self.ids.streams(ipi_hash)
        # (module streams)
        module_streams = []
        for module in self.modules:
            data, symbol_size, c13_size = module.stream(strings)
            module_streams.append((add(data), symbol_size, c13_size))
        # (the symbol record stream: globals, then publics)
        records, global_entries, public_entries = bytearray(), [], []
        for name, record, module_index, module_offset in self.globals:
            global_entries.append((name, len(records)))
            records += record
        public_offsets = []
        for name, segment, offset, function in self.publics:
            public_entries.append((name, len(records)))
            public_offsets.append(((segment, offset, name), len(records)))
            records += symbol(S_PUB32, struct.pack("<IIH", 2 if function else 0, offset, segment) + cstr(name))
        symbol_records = add(bytes(records))
        globals_stream = add(gsi_hash(global_entries))
        public_hash = gsi_hash(public_entries)
        address_map = b"".join(struct.pack("<I", at) for _, at in sorted(public_offsets))
        publics_stream = add(
            struct.pack("<IIIIHHII", len(public_hash), len(address_map), 0, 0, 0, 0, 0, len(self.section_headers))
            + public_hash
            + address_map
        )
        section_header_stream = add(b"".join(raw for *_, raw in self.section_headers))
        names_stream = add(strings.stream())
        streams[1] = self.info_stream(names_stream)
        streams[3] = self.dbi_stream(
            module_streams, globals_stream, publics_stream, symbol_records, section_header_stream
        )
        write_msf(path, streams)

    def info_stream(self, names_stream):
        names = b"/names\0"
        capacity = 8
        bucket = (hash_string_v1(b"/names") & 0xFFFF) % capacity
        table = struct.pack("<II", 1, capacity) + struct.pack("<II", 1, 1 << bucket) + struct.pack("<I", 0)
        table += struct.pack("<II", 0, names_stream)
        return (
            struct.pack("<III", 20000404, self.signature, self.age)
            + self.guid
            + struct.pack("<I", len(names))
            + names
            + table
            + struct.pack("<I", 20140508)
        )

    def dbi_stream(self, module_streams, globals_stream, publics_stream, symbol_records, section_header_stream):
        # (module info)
        modules = bytearray()
        for index, (module, (stream, symbol_size, c13_size)) in enumerate(zip(self.modules, module_streams)):
            first = module.contributions[0] if module.contributions else (0xFFFF, 0, 0xFFFFFFFF, 0)
            contribution = struct.pack("<HHiiIHHII", first[0], 0, first[1], first[2], first[3], index, 0, 0, 0)
            entry = (
                struct.pack("<I", 0)
                + contribution
                + struct.pack("<HHIIIHHIII", 0, stream, symbol_size, 0, c13_size, len(module.files), 0, 0, 0, 0)
            )
            entry += cstr(module.name) + cstr(module.object_name)
            modules += entry + bytes(-len(entry) % 4)
        # (section contributions, by section and offset)
        contributions = struct.pack("<I", 0xEFFE0000 + 19970605)
        for segment, offset, size, characteristics, index in sorted(
            (c + (i,) for i, m in enumerate(self.modules) for c in m.contributions)
        ):
            contributions += struct.pack("<HHiiIHHII", segment, 0, offset, size, characteristics, index, 0, 0, 0)
        # (the section map: each section, then the absolute one)
        section_map = bytearray(struct.pack("<HH", len(self.section_headers) + 1, len(self.section_headers) + 1))
        for number, (_, size, _, characteristics, _) in enumerate(self.section_headers, 1):
            flags = 0x8 | 0x100  # 32-bit addresses, a selector
            flags |= 0x1 if characteristics & 0x40000000 else 0
            flags |= 0x2 if characteristics & 0x80000000 else 0
            flags |= 0x4 if characteristics & 0x20000000 else 0
            section_map += struct.pack("<HHHHHHII", flags, 0, 0, number, 0xFFFF, 0xFFFF, 0, size)
        section_map += struct.pack(
            "<HHHHHHII", 0x8 | 0x200, 0, 0, len(self.section_headers) + 1, 0xFFFF, 0xFFFF, 0, 0xFFFFFFFF
        )
        # (source files: each module's)
        names, name_offsets, offsets = bytearray(), {}, []
        for module in self.modules:
            for name in module.files:
                if name not in name_offsets:
                    name_offsets[name] = len(names)
                    names += cstr(name)
                offsets.append(name_offsets[name])
        files = struct.pack("<HH", len(self.modules), len(offsets) & 0xFFFF)
        files += struct.pack(f"<{len(self.modules)}H", *[0] * len(self.modules))
        files += struct.pack(f"<{len(self.modules)}H", *[len(m.files) for m in self.modules])
        files += struct.pack(f"<{len(offsets)}I", *offsets) + bytes(names)
        files += bytes(-len(files) % 4)
        optional = struct.pack(
            "<11H",
            0xFFFF,
            0xFFFF,
            0xFFFF,
            0xFFFF,
            0xFFFF,
            section_header_stream,
            0xFFFF,
            0xFFFF,
            0xFFFF,
            0xFFFF,
            0xFFFF,
        )
        # (the edit-and-continue names: a string table with only "", which the modules' name indices (0) name)
        ec_names = Strings().stream()
        header = struct.pack(
            "<iIIHHHHHHiiiiiIiiHHI",
            -1,
            19990903,
            self.age,
            globals_stream,
            0x8E00,
            publics_stream,
            0,
            symbol_records,
            0,
            len(modules),
            len(contributions),
            len(section_map),
            len(files),
            0,
            0,
            len(optional),
            len(ec_names),
            0,
            0x14C,
            0,
        )
        return header + bytes(modules) + bytes(contributions) + bytes(section_map) + bytes(files) + ec_names + optional


def write_msf(path, streams):
    """Write STREAMS as an MSF 7.00 file: 4096-byte blocks, the free block maps at blocks 1 and 2 of each 4096."""
    blocks = {}
    next_block = [3]

    def allocate():
        while next_block[0] % BLOCK in (1, 2):
            next_block[0] += 1
        block = next_block[0]
        next_block[0] += 1
        return block

    lists = []
    for data in streams:
        indices = []
        for at in range(0, len(data), BLOCK):
            block = allocate()
            blocks[block] = data[at : at + BLOCK]
            indices.append(block)
        lists.append(indices)
    directory = struct.pack(f"<I{len(streams)}I", len(streams), *[len(d) for d in streams])
    directory += b"".join(struct.pack(f"<{len(l)}I", *l) for l in lists)
    directory_blocks = []
    for at in range(0, len(directory), BLOCK):
        block = allocate()
        blocks[block] = directory[at : at + BLOCK]
        directory_blocks.append(block)
    block_map = allocate()
    blocks[block_map] = struct.pack(f"<{len(directory_blocks)}I", *directory_blocks)
    count = next_block[0]
    # (the free block map: a bit per block, set when free; every block of the file is in use)
    bits = bytearray(b"\xff" * BLOCK)
    for block in range(min(count, BLOCK * 8)):
        bits[block // 8] &= ~(1 << (block % 8)) & 0xFF
    superblock = MSF_MAGIC + struct.pack("<IIIIII", BLOCK, 1, count, len(directory), 0, block_map)
    with open(path, "wb") as out:
        for block in range(count):
            if block == 0:
                data = superblock
            elif block == 1:
                data = bytes(bits)
            elif block % BLOCK in (1, 2):
                data = b"\xff" * BLOCK
            else:
                data = blocks.get(block, b"")
            out.write(data + bytes(BLOCK - len(data)))
