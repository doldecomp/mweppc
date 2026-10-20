#!/usr/bin/env python3

import argparse
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import formats, project
from mw.pe import original

WHICH = {
    0: ("None", []),
    1: ("FTypeCreator", [("fc", "p"), ("iscreator", "b")]),
    2: ("FilePath", [("fflags", "b"), ("defaultstr", "s"), ("filename", "p"), ("maxlen", "i")]),
    3: ("Number", [("size", "b"), ("fit", "b"), ("lo", "i"), ("hi", "i"), ("num", "p")]),
    4: ("String", [("maxlen", "h"), ("pstring", "b"), ("str", "p")]),
    5: ("Id", [("maxlen", "h"), ("pstring", "b"), ("str", "p")]),
    6: ("Sym", [("maxlen", "h"), ("pstring", "b"), ("str", "p")]),
    7: ("OnOff", [("var", "p")]),
    8: ("OffOn", [("var", "p")]),
    9: ("Mask", [("size", "b"), ("ormask", "i"), ("andmask", "i"), ("num", "p")]),
    10: ("Toggle", [("size", "b"), ("mask", "i"), ("num", "p")]),
    11: ("Set", [("size", "b"), ("value", "i"), ("num", "p")]),
    12: ("SetString", [("value", "s"), ("pstring", "b"), ("var", "p")]),
    13: ("Generic", [("parse", "f"), ("var", "p"), ("help", "s")]),
    14: ("IfArg", [("parg", "P"), ("helpa", "s"), ("pnone", "P"), ("helpn", "s")]),
    15: ("Setting", [("parse", "f"), ("valuename", "s")]),
}
SIZES = {"b": 1, "h": 2, "i": 4, "p": 4, "s": 4, "f": 4, "P": 4}
TYPES = {
    "None": "PARAM_T",
    "FTypeCreator": "FTYPE_T",
    "FilePath": "FILEPATH_T",
    "Number": "NUM_T",
    "String": "STRING_T",
    "Id": "STRING_T",
    "Sym": "STRING_T",
    "OnOff": "ONOFF_T",
    "OffOn": "OFFON_T",
    "Mask": "MASK_T",
    "Toggle": "TOGGLE_T",
    "Set": "SET_T",
    "SetString": "SETSTRING_T",
    "Generic": "GENERIC_T",
    "IfArg": "IFARG_T",
    "Setting": "SETTING_T",
}


class Image:
    def __init__(self, version):
        self.pe = original(version)
        self.fixups = set(self.pe.base_relocations)
        self.names = {}
        self.sizes = {}
        for symbol in formats.read_symbols(project.version(version).config / "symbols.txt"):
            if symbol.size is not None:
                self.names.setdefault(symbol.address, symbol.name)
                self.sizes.setdefault(symbol.address, symbol.size)

    def u8(self, a):
        return self.pe.read(a, 1)[0]

    def u16(self, a):
        return struct.unpack("<H", self.pe.read(a, 2))[0]

    def u32(self, a):
        return struct.unpack("<I", self.pe.read(a, 4))[0]

    def ptr(self, a):
        return self.u32(a) if a in self.fixups else None

    def cstr(self, a):
        data = self.pe.read(a, 4096)
        return data[: data.index(b"\0")]


class Walk:
    def __init__(self, image):
        self.image = image
        self.objects = {}
        self.data = None

    def add(self, address, kind, size, fields):
        if address in self.objects:
            return False
        self.objects[address] = (kind, size, fields)
        return True

    def string(self, address):
        if address is not None and address not in self.objects:
            text = self.image.cstr(address)
            self.add(address, "string", len(text) + 1, text)

    def tool(self, address):
        im = self.image
        section = im.pe.section_for_address(address)
        self.data = (section.virtual_address, section.virtual_address + section.virtual_size)
        f = [im.u32(address + 4 * i) for i in range(15)]
        self.add(address, "ParserTool", 60, f)
        self.string(f[6])
        self.string(f[7])
        self.add(f[5], "prefs", 4 * f[4], [im.u32(f[5] + 4 * i) for i in range(f[4])])
        for i in range(f[4]):
            self.string(im.u32(f[5] + 4 * i))
        lists = [im.u32(f[9] + 4 * i) for i in range(f[8])]
        self.add(f[9], "optLists", 4 * f[8], lists)
        panels = [
            (im.u32(f[11] + 12 * i), im.u32(f[11] + 12 * i + 4), im.u32(f[11] + 12 * i + 8)) for i in range(f[10])
        ]
        self.add(f[11], "prefPanels", 12 * f[10], panels)
        for name, data, size in panels:
            self.string(name)
            self.add(data, "panel", size, self.image.cstr(name))
        for a in lists:
            self.option_list(a)

    def option_list(self, address):
        if address is None or not self.add(address, "OptionList", 12, None):
            return
        im = self.image
        self.string(im.ptr(address))
        array = im.ptr(address + 8)
        entries = []
        if array is not None:
            a = array
            while im.u32(a):
                entries.append(im.u32(a))
                a += 4
            self.add(array, "OptionArray", 4 * len(entries) + 4, entries)
            for e in entries:
                self.option(e)

    def option(self, address):
        if not self.add(address, "Option", 24, None):
            return
        im = self.image
        self.string(im.ptr(address))
        self.param(im.ptr(address + 8))
        self.option_list(im.ptr(address + 12))
        self.option_list(im.ptr(address + 16))
        self.string(im.ptr(address + 20))

    def param(self, address):
        while address is not None and address not in self.objects:
            im = self.image
            which = im.u8(address)
            kind, layout = WHICH[which]
            size = 10 + sum(SIZES[t] for _, t in layout)
            self.add(address, "param", size, kind)
            self.string(im.ptr(address + 2))
            offset = 10
            for field, t in layout:
                if t == "s":
                    self.string(im.ptr(address + offset))
                elif t == "P":
                    self.param(im.ptr(address + offset))
                offset += SIZES[t]
            address = im.ptr(address + 6)
        self.pending_strings()

    def pending_strings(self):
        im = self.image
        for address, (kind, size, _) in list(self.objects.items()):
            if kind != "param":
                continue
            _, layout = WHICH[im.u8(address)]
            offset = 10
            for field, t in layout:
                value = im.ptr(address + offset) if t == "p" and field == "var" and im.u8(address) == 13 else None
                if value is not None and self.data[0] <= value < self.data[1] and not self.inside(value):
                    self.string(value)
                offset += SIZES[t]

    def inside(self, value):
        return any(a <= value < a + o[1] for a, o in self.objects.items() if o[0] == "panel" or a == value)


def report(walk, lo, hi):
    position = lo
    for address in sorted(walk.objects):
        kind, size, fields = walk.objects[address]
        if not lo <= address < hi:
            continue
        if address > position:
            print(f"  {position:08x} {address - position:5} unexplained")
        elif address < position:
            print(f"  {address:08x} overlaps the object before")
        label = fields.decode("latin1")[:60] if kind == "string" else (fields if kind in ("param", "panel") else "")
        print(f"{address:08x} {size:5} {kind:12} {label!r}")
        position = max(position, address + size)
    if position < hi:
        print(f"  {position:08x} {hi - position:5} unexplained")


PANELS = {
    "CmdLine Panel": ("pCmdLine", "PCmdLine"),
    "CmdLine Compiler Panel": ("pCmdLineCompiler", "PCmdLineCompiler"),
    "CmdLine Linker Panel": ("pCmdLineLinker", "PCmdLineLinker"),
    "PPC EABI CodeGen": ("pBackEnd", "PBackEnd"),
    "PPC EABI Linker": ("pLinker", "PLinker"),
    "PPC EABI Disassembler": ("pDisassembler", "PDisassembler"),
    "PPC EABI Project": ("pProject", "PProject"),
    "CmdLine Extras EPPC": ("pCLTExtras", "PCLTExtras"),
    "C/C++ Compiler": ("pFrontEndC", "PFrontEndC"),
    "C/C++ Warnings": ("pWarningC", "PWarningC"),
    "EPPC Global Optimizer": ("pGlobalOptimizer", "PGlobalOptimizer"),
}
LISTS = [
    ("General Command-Line Options", "CmdLine"),
    ("Preprocessing, Precompiling", "CmdLineCompiler"),
    ("Command-Line Linker Options", "CmdLineLinker"),
    ("Front-End C/C++ Language Options", "FrontEndC"),
    ("Debugging Control Options", "Debugging"),
    ("Optimizer Options", "Optimizer"),
    ("C/C++ Warning Options", "WarningC"),
    ("Embedded PowerPC Options", "BackEnd"),
    ("Embedded PPC Project Options", "Project"),
    ("ELF Linker Options", "Linker"),
    ("ELF Linker Address Options", "LinkerAddresses"),
    ("Embedded PowerPC Disassembler Options", "Disassembler"),
]
SCALARS = {
    "Boolean": 1,
    "UInt8": 1,
    "SInt8": 1,
    "char": 1,
    "unsigned char": 1,
    "SInt16": 2,
    "UInt16": 2,
    "UInt32": 4,
    "SInt32": 4,
    "int": 4,
    "OSType": 4,
}
GLOBALS = {"_parseopts": ("parseopts", "ParseOptsType")}


def struct_layouts(header):
    layouts = {}
    for m in re.finditer(r"typedef struct (\w+) \{(.*?)\} \1;", (project.ROOT / header).read_text(), re.DOTALL):
        offset, fields = 0, []
        for line in re.sub(r"/\*.*?\*/|//[^\n]*", "", m.group(2), flags=re.DOTALL).split(";"):
            f = re.match(r"\s*(.*?)\s*(\w+)(?:\[(\d+)\])?\s*$", line)
            if not f or not f.group(2):
                continue
            size = 4 if "*" in f.group(1) else SCALARS[f.group(1).removeprefix("const ")]
            offset += offset % 2 if size > 1 else 0
            count = int(f.group(3)) if f.group(3) else None
            fields.append((offset, size * (count or 1), f.group(2), size if count else None))
            offset += size * (count or 1)
        layouts[m.group(1)] = fields
    return layouts


def c_string(data):
    out = []
    for c in data:
        ch = chr(c)
        out.append(
            {"\n": "\\n", "\t": "\\t", "\b": "\\b", '"': '\\"', "\\": "\\\\"}.get(ch)
            or (ch if 32 <= c < 127 else f"\\{c:03o}")
        )
    return '"' + "".join(out) + '"'


class Writer:
    def __init__(self, walk, header, struct_header):
        self.walk = walk
        self.im = walk.image
        self.layouts = struct_layouts(header) | struct_layouts(struct_header)
        self.panels = [(a, o[1], PANELS[o[2].decode()]) for a, o in walk.objects.items() if o[0] == "panel"]
        for a, name in self.im.names.items():
            if name in GLOBALS:
                var, type_ = GLOBALS[name]
                last = self.layouts[type_][-1]
                self.panels.append((a, last[0] + last[1], (var, type_)))
        self.names = {}

    def name_objects(self, roots):
        im = self.im
        named = []
        for root in roots:
            help_ = im.cstr(im.ptr(root)).decode("latin1")
            name = next(n for prefix, n in LISTS if help_.startswith(prefix))
            named.append((root, "optlst" + name))
        named.sort()
        previous, files = 0, []
        for root, name in named:
            members = [
                a
                for a in sorted(self.walk.objects)
                if previous < a <= root and self.walk.objects[a][0] in ("param", "Option", "OptionArray", "OptionList")
            ]
            for i, a in enumerate(members):
                self.names[a] = name if a == root else f"{name}_{i:03d}"
            files.append((name, members))
            previous = root
        return files

    def ref(self, value, cast=""):
        if value is None:
            return "0"
        kind = self.walk.objects.get(value, (None,))[0]
        if kind == "string":
            return cast + c_string(self.walk.objects[value][2])
        if value in self.names:
            return cast + "&" + self.names[value]
        for start, size, (var, type_) in self.panels:
            if start <= value < start + size:
                offset = value - start
                for f_offset, f_size, field, element in self.layouts[type_]:
                    if f_offset == offset:
                        return cast + f"&{var}.{field}"
                    if element and f_offset < offset < f_offset + f_size:
                        return cast + f"&{var}.{field}[{(offset - f_offset) // element}]"
                return f"{cast}((char *)&{var} + {offset})"
        name = self.im.names.get(value)
        if name:
            return cast + ("" if self.im.pe.section_for_address(value).name == ".text" else "&") + c_name(name)
        start = max((a for a in self.im.sizes if a <= value < a + self.im.sizes[a]), default=None)
        if start is None:
            raise SystemExit(f"0x{value:x}: no symbol there or around it")
        return f"{cast}((char *)&{c_name(self.im.names[start])} + {value - start})"

    def param(self, address):
        im = self.im
        kind, layout = WHICH[im.u8(address)]
        values = [
            str(im.u8(address)),
            f"0x{im.u8(address + 1):02X}",
            self.ref(im.ptr(address + 2)),
            self.ref(im.ptr(address + 6), "(PARAM_T *)"),
        ]
        if kind == "None":
            return f"PARAM_T {self.names[address]} = {{" + ", ".join(values) + "};"
        fields, offset = values, 10
        for field, t in layout:
            if t == "b":
                fields.append(str(im.u8(address + offset)))
            elif t == "h":
                fields.append(str(struct.unpack("<h", im.pe.read(address + offset, 2))[0]))
            elif t == "i":
                fields.append(f"0x{im.u32(address + offset):X}")
            elif t in "psfP" and im.ptr(address + offset) is None and im.u32(address + offset):
                fields.append(f"(void *){im.u32(address + offset)}")
            elif t in "psfP":
                fields.append(
                    self.ref(im.ptr(address + offset), "(PARAM_T *)" if t == "P" else "(void *)" if t in "pf" else "")
                )
            offset += SIZES[t]
        return f"{TYPES[kind]} {self.names[address]} = {{" + ", ".join(fields) + "};"

    def option(self, address):
        im = self.im
        values = [
            self.ref(im.ptr(address)),
            f"0x{im.u32(address + 4):X}",
            self.ref(im.ptr(address + 8), "(PARAM_T *)"),
            self.ref(im.ptr(address + 12)),
            self.ref(im.ptr(address + 16)),
            self.ref(im.ptr(address + 20)),
        ]
        return f"Option {self.names[address]} = {{" + ", ".join(values) + "};"

    def declaration(self, address):
        kind = self.walk.objects[address][0]
        name = self.names[address]
        if kind == "param":
            return f"extern {TYPES[WHICH[self.im.u8(address)][0]]} {name};"
        return {
            "Option": f"extern Option {name};",
            "OptionArray": f"extern Option *{name}[];",
            "OptionList": f"extern OptionList {name};",
        }[kind]

    def definition(self, address):
        kind, size, fields = self.walk.objects[address]
        im = self.im
        if kind == "param":
            return self.param(address)
        if kind == "Option":
            return self.option(address)
        if kind == "OptionArray":
            items = "".join(f"\n    &{self.names[e]}," for e in fields)
            return f"Option *{self.names[address]}[] = {{{items}\n    0\n}};"
        return (
            f"OptionList {self.names[address]} = {{{self.ref(im.ptr(address))}, 0x{im.u32(address + 4):X}, "
            f"{self.names.get(im.ptr(address + 8), '0')}}};"
        )


def c_name(symbol):
    m = re.fullmatch(r"_(\w+?)(?:@\d+)?", symbol)
    return m.group(1) if m else symbol


def main():
    parser = argparse.ArgumentParser(
        description="The command line parser's option tables of a glue unit, read from the original's .data as C.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version")
    parser.add_argument("root", nargs="?", help="the address of the unit's ParserTool (hexadecimal)")
    parser.add_argument(
        "--string-table",
        action="append",
        default=[],
        metavar="ADDRESS=NAME",
        help="instead: write the NULL-terminated string table at ADDRESS (a resource's STRnnnn) to OUT",
    )
    parser.add_argument("--range", nargs=2, metavar=("START", "END"), help="with --report: only these addresses")
    parser.add_argument("--report", action="store_true", help="list the objects the walk finds instead of writing C")
    parser.add_argument("--header", default="include/driver/pref_structs.h", help="the preference panels' types")
    parser.add_argument("--struct-header", default="include/driver/ParserFace.h", help="the other structs' types")
    parser.add_argument("-o", "--out", help="directory for the .opt files")
    parser.add_argument(
        "--name",
        action="append",
        default=[],
        metavar="ADDRESS=NAME",
        help="the C name of a variable at ADDRESS (a static of the unit, which symbols.txt may lack)",
    )
    args = parser.parse_args()
    try:
        generate(parser, args)
    except project.Error as e:
        sys.exit(f"error: {e}")


def generate(parser, args):
    if not args.out and not args.report:
        parser.error("-o OUT is needed (or --report)")
    if not args.root and not args.string_table:
        parser.error("ROOT is needed (or --string-table)")
    if args.string_table:
        if not args.out:
            parser.error("--string-table writes to -o OUT")
        im = Image(args.version)
        lines = []
        for item in args.string_table:
            address, name = item.split("=")
            a, strings = int(address, 16), []
            while im.ptr(a) is not None:
                strings.append(c_string(im.cstr(im.ptr(a))))
                a += 4
            lines.append(f"static const char *{name}[] = {{")
            lines += [f"    {s}," for s in strings] + ["    NULL", "};"]
        Path(args.out).write_text("\n".join(lines) + "\n")
        return
    walk = Walk(Image(args.version))
    walk.tool(int(args.root, 16))
    lo, hi = (int(x, 16) for x in args.range) if args.range else (min(walk.objects), max(walk.objects) + 60)
    if args.report:
        report(walk, lo, hi)
        return
    for item in args.name:
        address, name = item.split("=")
        walk.image.names[int(address, 16)] = "_" + name
    writer = Writer(walk, args.header, args.struct_header)
    tool = walk.objects[int(args.root, 16)][2]
    files = writer.name_objects(walk.objects[tool[9]][2])
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    for name, members in files:
        lines = []
        forward = sorted({r for m in members for r in referenced(writer, m) if r in writer.names and r > m})
        lines += [writer.declaration(a) for a in forward]
        lines += [writer.definition(a) for a in members]
        (out / f"Opts{name[6:]}.opt").write_text("\n".join(lines) + "\n")
        print(out / f"Opts{name[6:]}.opt", len(members))


def referenced(writer, address):
    im = writer.im
    kind, size, _ = writer.walk.objects[address]
    return {im.u32(a) for a in range(address, address + size) if a in im.fixups}


if __name__ == "__main__":
    main()
