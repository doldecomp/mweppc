#!/usr/bin/env python3

import argparse
import re
import struct
import subprocess
import sys
from bisect import bisect_right
from collections import defaultdict
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, codeview, config, formats, names, project, verdict
from mw.coff import DIR32, EXTERNAL, REL32, STATIC, Object
from mw.pe import original

SECTIONS = [
    (".text", "code", 16),
    (".rdata", "rodata", 4),
    (".exc", "rodata", 4),
    (".data", "data", 8),
    (".rsrc", "data", 4),
    (".idata", "data", 4),
    (".CRT", "data", 4),
    (".tls", "data", 4),
    (".bss", "bss", 8),
]
SECTION_ORDER = [name for name, _, _ in SECTIONS]
DATA_SECTIONS = (".rdata", ".data", ".bss", ".tls")
DECORATIONS = {"GetShortPathNameA": 12}
RUNTIME = {"cexc.obj": "src/runtime/ExceptionX86.cpp", "ccinit.obj": "src/msl/abort_exit.c"}
BUILD_STAMP = ("src/driver/CmdLineBuildDate.c", 0x20)


def monotone(items, count):
    best = [(0.0, None)] * count
    prefixes = []
    for index, (_, candidates) in enumerate(items):
        prefix, running = [], (0.0, None)
        for unit in range(count):
            if best[unit][0] > running[0]:
                running = best[unit]
            prefix.append(running)
        for unit, weight in candidates.items():
            score, node = prefix[unit]
            if score + weight > best[unit][0]:
                best[unit] = (score + weight, (index, unit, node))
        prefixes.append(max(b[0] for b in best))
    node, chosen = max(best, key=lambda b: b[0])[1], {}
    while node:
        index, unit, node = node
        chosen[items[index][0]] = unit
    return chosen, prefixes


def boundaries(chosen, units, low, high, fixups=frozenset(), placed=None, referenced=(), keep_owned=False):
    first, last = {}, {}
    for address in sorted(chosen):
        first.setdefault(chosen[address], address)
        last[chosen[address]] = address
    ordered = sorted(first)
    placed = placed or {}
    starts, previous = {}, None
    for unit in ordered:
        address = first[unit]
        start = address
        if unit in placed:
            start = placed[unit][0]
        else:
            if previous is not None and previous >= address:
                previous = None
            for align in (8, 4, 2, 1):
                candidate = address - address % align
                if (previous is None or candidate > previous) and not any(
                    f in fixups for f in range(candidate - 3, candidate)
                ):
                    start = candidate
                    break
        starts[unit] = start
        previous = placed[unit][0] + placed[unit][1] - 1 if unit in placed else last[unit]
    if ordered and ordered[0] not in placed:
        starts[ordered[0]] = low
    out = {}
    for i, unit in enumerate(ordered):
        end = starts[ordered[i + 1]] if i + 1 < len(ordered) else high
        if unit in placed:
            placed_end = placed[unit][0] + placed[unit][1]
            if keep_owned:
                placed_end = max(placed_end, last[unit] + 1)
            end = min(end, placed_end) if i + 1 < len(ordered) else end
        out[units[unit]] = (starts[unit], end)
    keys = [units[u] for u in ordered]
    for i in range(len(keys) - 1):
        end, following = out[keys[i]][1], out[keys[i + 1]][0]
        if end < following:
            if ordered[i + 1] in placed and any(end <= r < following for r in referenced):
                out[keys[i + 1]] = (end, out[keys[i + 1]][1])
            elif (
                keep_owned
                and ordered[i] in placed
                and ordered[i + 1] in placed
                and end % 8 == 0
                and following - end >= 8
                and ordered[i + 1] - ordered[i] > 1
            ):
                out[units[ordered[i] + 1]] = (end, following)
            elif ordered[i] in placed and following % 8 == 0 and following - end < 8:
                out[keys[i]] = (out[keys[i]][0], following)
            elif ordered[i + 1] in placed:
                out[keys[i]] = (out[keys[i]][0], following)
            else:
                out[keys[i + 1]] = (end, out[keys[i + 1]][1])
    if ordered and out[keys[0]][0] > low:
        out[keys[0]] = (low, out[keys[0]][1])
    return out


def longest_run(mask):
    runs, start = [], None
    for i, m in enumerate(mask):
        if m and start is None:
            start = i
        elif not m and start is not None:
            runs.append((start, i))
            start = None
    if start is not None:
        runs.append((start, len(mask)))
    return max(runs, key=lambda r: r[1] - r[0]) if runs else None


def find_masked(haystack, base, image, mask, minimum, limit=2):
    run = longest_run(mask)
    if run is None or run[1] - run[0] < minimum:
        return []
    a, b = run
    needle = image[a:b]
    hits, at = [], haystack.find(needle)
    while at >= 0 and len(hits) < limit:
        where = at - a
        if (
            where >= 0
            and where + len(image) <= len(haystack)
            and all(not mask[i] or haystack[where + i] == image[i] for i in range(len(image)))
        ):
            hits.append(base + where)
        at = haystack.find(needle, at + 1)
    return hits


def section_layout(obj, name, dropped=frozenset()):
    offsets, cursor = {}, 0
    for section in obj.sections:
        if section.name.split("$")[0] != name:
            continue
        if dropped and section.relocations:
            targets = [obj.by_index.get(r.symbol) for r in section.relocations]
            if all(t and t.function and names.c_name(t.name) in dropped for t in targets):
                continue
        cursor = (cursor + section.alignment - 1) // section.alignment * section.alignment
        offsets[section.number] = cursor
        cursor += section.size
    return offsets, cursor


class Layout:
    def __init__(self, version):
        self.version = project.version(version)
        verdict.load(self.version)
        self.cfg = config.version_config(self.version)
        self.matching = set(self.cfg.matching)
        self.pe = original(self.version)
        self.section = {s.name: s for s in self.pe.sections}
        self.fixups = self.pe.base_relocations
        self.sorted_fixups = sorted(self.fixups)
        self.fixup_set = frozenset(self.fixups)
        self.value_at = {at: self.pe.word(at) for at in self.fixups}
        self.functions = sorted(config.functions(self.version), key=lambda f: f.address)
        self.bindings = config.bindings(self.version)
        self.sources = list(config.sources())
        self.objects = {}
        for source in self.sources:
            path = build.object_path(self.version, source)
            if path.exists():
                self.objects[source] = Object.read(path)
        self.ranges = defaultdict(dict)
        self.warnings = []
        self.bss_statics = self.read_bss_statics()

    def read_bss_statics(self):
        out = {}
        with_symbols = {c.source for c in build.compilations(self.version) if not c.pro4}
        for source, obj in self.objects.items():
            bss = [section for section in obj.sections if section.name == ".bss"]
            if not bss or source not in with_symbols:
                continue
            path = build.object_path(self.version, source, debug=True)
            if not path.exists() or path.stat().st_mtime < build.object_path(self.version, source).stat().st_mtime:
                raise project.Error(f"{project.relative(path)} is missing or out of date: run ninja")
            debug = codeview.Object(path)
            sizes = [section["size"] for section in debug.sections if section["name"] == ".bss"]
            if sizes != [bss[0].size]:
                continue
            statics = []
            for at, kind, body, section in debug.object_symbols():
                if kind != 0x1007:
                    continue
                target = next((debug.symbols[i] for o, i, _ in section["relocs"] if o == at + 8), None)
                if target and target["name"] == ".bss":
                    statics.append((struct.unpack_from("<I", body, 4)[0], "_" + codeview.pstring(body, 10)[0]))
            if statics:
                out[source] = sorted(statics)
        return out

    def warn(self, message):
        self.warnings.append(message)

    def section_of(self, address):
        try:
            return self.pe.section_for_address(address).name
        except ValueError:
            return None

    def gather_evidence(self):
        text = self.section[".text"]
        code = self.pe.data[text.file_offset : text.file_offset + text.virtual_size]
        found, anchors, externals, placed = {}, {}, defaultdict(lambda: defaultdict(int)), {}
        rows = {(f.source, f.name): f.address for f in self.functions}
        for source, obj in self.objects.items():
            found[source], votes = {}, defaultdict(lambda: defaultdict(int))
            for section in obj.sections:
                if not section.code:
                    continue
                members = sorted(
                    (s.value, s.name)
                    for s in obj.symbols
                    if s.section == section.number and s.function and s.storage in (EXTERNAL, STATIC)
                )
                for k, (start, name) in enumerate(members):
                    end = members[k + 1][0] if k + 1 < len(members) else section.size
                    body = section.data[start:end]
                    sites = [
                        (r.offset - start, r.symbol, r.kind) for r in section.relocations if start <= r.offset < end
                    ]
                    mask = bytearray(b"\1" * len(body))
                    for o, _, _ in sites:
                        mask[o : o + 4] = bytes(4)
                    hits = find_masked(code, text.virtual_address, body, mask, 8)
                    if len(hits) > 1:
                        hits = [hit for hit in hits if hit == rows.get((source, names.c_name(name)))]
                    if len(hits) != 1:
                        continue
                    address = hits[0]
                    found[source].setdefault(address, []).append((name, len(body)))
                    for o, index, kind in sites:
                        target = obj.symbol(index)
                        stored = struct.unpack_from("<I", body, o)[0]
                        value = struct.unpack_from("<I", code, address - text.virtual_address + o)[0]
                        if kind == DIR32:
                            resolved = (value - stored) & 0xFFFFFFFF
                        elif kind == REL32:
                            resolved = (value + address + o + 4 - stored) & 0xFFFFFFFF
                        else:
                            continue
                        if target.section > 0:
                            if not obj.section(target.section).code:
                                votes[target.section][resolved - target.value] += 1
                                placed[source, target.section, target.value] = resolved
                        elif target.section == 0 and target.storage == EXTERNAL:
                            externals[target.name][resolved] += 1
            anchors[source] = {n: max(c, key=c.get) for n, c in votes.items()}
        self.found, self.anchors, self.externals, self.data_placed = found, anchors, externals, placed

    def name_functions(self):
        decorated = {}
        for name in [s.name for obj in self.objects.values() for s in obj.symbols] + list(self.bindings):
            if m := re.fullmatch(r"(_\w+)@(\d+)", name):
                decorated.setdefault(m[1], name)
            elif m := re.fullmatch(r"__imp_(_\w+)@(\d+)", name):
                decorated.setdefault(m[1], name[6:])

        by_slot = {address: name for name, address in self.bindings.items() if name.startswith("__imp_")}
        self.import_names, thunk_names = {}, {}
        for item in self.pe.imports:
            if item.slot in by_slot:
                symbol = by_slot[item.slot]
            elif item.name is None:
                raise project.Error(
                    f"{item.dll} ordinal {item.number} (at 0x{item.slot:08x}) has no binding: "
                    f"bind its __imp_ name in config/{self.version}/bindings.json"
                )
            else:
                c = "_" + item.name
                symbol = "__imp_" + (
                    decorated.get(c) or (f"{c}@{DECORATIONS[item.name]}" if item.name in DECORATIONS else c)
                )
            self.import_names[item.slot] = symbol
            thunk_names[item.slot] = symbol[6:]

        self.claims = defaultdict(set)
        for source, here_by_address in self.found.items():
            for address, here in here_by_address.items():
                self.claims[address] |= {(source, name) for name, _ in here}

        self.rows, seen = [], {}
        for f in self.functions:
            code = self.pe.read(f.address, min(f.size, 6))
            if (
                f.size == 6
                and code[:2] == b"\xff\x25"
                and struct.unpack_from("<I", code, 2)[0] in thunk_names
                and f.source is None
            ):
                name, thunk = thunk_names[struct.unpack_from("<I", code, 2)[0]], True
            else:
                name = f.name if names.is_mangled(f.name) else decorated.get("_" + f.name, "_" + f.name)
                thunk = False
                given = {n for _, n in self.claims.get(f.address, ())}
                if given and name not in given:
                    self.warn(
                        f"0x{f.address:08x} {f.name}: the compiled code of "
                        f"{', '.join(sorted({s for s, _ in self.claims[f.address]}))} there is "
                        f"{', '.join(sorted(given))}"
                    )
            if name in seen:
                raise project.Error(f"0x{seen[name]:08x} and 0x{f.address:08x} are both {name}")
            seen[name] = f.address
            self.rows.append(
                dict(name=f.name, address=f.address, size=f.size, source=f.source, symbol=name, thunk=thunk)
            )

    def text_units(self):
        labels = []
        for row in self.rows:
            if row["thunk"]:
                labels.append("thunk")
                continue
            source = row["source"]
            claimed = {c for c, _ in self.claims.get(row["address"], ())}
            matching_claims = claimed & self.matching
            if source and claimed and source not in claimed:
                self.warn(
                    f"0x{row['address']:08x} {row['name']}: its source is {source}, but the compiled code of "
                    f"{', '.join(sorted(claimed))} is there"
                )
            if len(claimed) == 1 and source not in claimed:
                source = next(iter(claimed))
            elif len(matching_claims) == 1 and source not in claimed:
                source = next(iter(matching_claims))
            labels.append(source)
        for i, label in enumerate(labels):
            if label is None:
                labels[i] = labels[i - 1] if i else "unknown/start"

        def runs():
            out = []
            for i, label in enumerate(labels):
                if out and out[-1][0] == label:
                    out[-1][2] = i + 1
                else:
                    out.append([label, i, i + 1])
            return out

        longest = {}
        for label, start, end in runs():
            if label != "thunk" and end - start > longest.get(label, (0, 0))[1] - longest.get(label, (0, 0))[0]:
                longest[label] = (start, end)
        for label, start, end in runs():
            if label != "thunk" and longest[label] != (start, end):
                for i in range(start, end):
                    labels[i] = None
        for i, label in enumerate(labels):
            if label is None:
                before = next((labels[j] for j in range(i - 1, -1, -1) if labels[j] not in (None, "thunk")), None)
                after = next((labels[j] for j in range(i + 1, len(labels)) if labels[j] not in (None, "thunk")), None)
                labels[i] = before or after
        units = []
        for label, start, end in runs():
            name = label if label != "thunk" else f"imports/thunks_{self.rows[start]['address']:08x}"
            if any(u[0] == name for u in units):
                raise project.Error(f"{name}: two runs of functions after the strays were merged")
            units.append([name, start, end])
        for source, after in self.cfg.raw.get("data_units", {}).items():
            k = next((i for i, u in enumerate(units) if u[0] == after), None)
            if k is None:
                raise project.Error(
                    f"config/{self.version}/config.json: data_units: {source} follows {after}, which is not a unit"
                )
            units.insert(k + 1, [source, units[k][2], units[k][2]])
        for row, label in zip(self.rows, labels):
            row["unit"] = label

        text = self.section[".text"]
        self.unit_names = [u[0] for u in units]
        self.unit_index = {u: i for i, u in enumerate(self.unit_names)}
        self.function_starts = [r["address"] for r in self.rows]
        self.unit_of_function = []
        for index, (name, start, end) in enumerate(units):
            self.unit_of_function += [index] * (end - start)
        for i, (name, start, end) in enumerate(units):
            if start == end:
                continue
            begin = self.rows[start]["address"]
            following = next((u[1] for u in units[i + 1 :]), None)
            stop = (
                self.rows[following]["address"]
                if following is not None and following < len(self.rows)
                else text.virtual_address + text.virtual_size
            )
            align = 16
            for row in self.rows[start:end]:
                align = min(align, row["address"] & -row["address"])
            self.ranges[name][".text"] = (begin, stop, align)
        self.layouts = {s: obj for s, obj in self.objects.items() if s in self.unit_index}
        rows = defaultdict(set)
        for f in self.functions:
            rows[f.source].add(f.name)
        self.dropped = {
            source: frozenset(
                names.c_name(s.name)
                for s in obj.symbols
                if s.function and s.section > 0 and names.c_name(s.name) not in rows[source]
            )
            for source, obj in self.layouts.items()
            if source in self.matching
        }

    def unit_at_code(self, address):
        i = bisect_right(self.function_starts, address) - 1
        return self.unit_of_function[i] if i >= 0 else None

    def exception_ranges(self):
        exc, rdata = self.section[".exc"], self.section[".rdata"]
        rdata_low, rdata_high = rdata.virtual_address, rdata.virtual_address + rdata.virtual_size
        records, pcs = [], {}
        for at in range(exc.virtual_address, exc.virtual_address + exc.virtual_size - 4, 8):
            pc, action = struct.unpack_from("<II", self.pe.read(at, 8))
            unit = self.unit_at_code(pc)
            if unit is None:
                raise project.Error(f"the exception record at 0x{at:08x} is for 0x{pc:08x}, before the first function")
            records.append((at, unit, action))
            pcs.setdefault(action, pc)
        self.exc_end = records[-1][0] + 8 if records else exc.virtual_address
        actions = sorted({action for _, _, action in records})
        starts = {}
        self.exc_tables = {}
        for k, action in enumerate(actions):
            following = actions[k + 1] & ~3 if k + 1 < len(actions) else rdata_high
            back = [
                self.value_at[at]
                for at in self.fixups
                if action <= at < following and rdata_low <= self.value_at[at] < action
            ]
            starts[action] = min([action, *back]) & ~3
            self.exc_tables[action] = (pcs[action], starts[action], following)
        first = {}
        for _, unit, action in records:
            first[unit] = min(first.get(unit, rdata_high), starts[action])
        for at in self.fixups:
            if rdata_low <= at < rdata_high and self.section_of(self.value_at[at]) == ".text":
                unit = self.unit_at_code(self.value_at[at])
                if unit not in first:
                    first[unit] = at & ~3
        first = sorted((start, unit) for unit, start in first.items())
        for i, (start, unit) in enumerate(first):
            stop = first[i + 1][0] if i + 1 < len(first) else rdata_high
            self.ranges[self.unit_names[unit]][".rdata"] = (start if i else rdata_low, stop, 4)
        for i, (at, unit, _) in enumerate(records):
            if i and records[i - 1][1] == unit:
                continue
            j = i
            while j + 1 < len(records) and records[j + 1][1] == unit:
                j += 1
            self.ranges[self.unit_names[unit]][".exc"] = (
                at,
                records[j + 1][0] if j + 1 < len(records) else self.exc_end,
                4,
            )

    def same_bytes(self, base, offsets, obj):
        for number, at in offsets.items():
            section = obj.section(number)
            body = bytearray(section.data)
            if not body:
                continue
            there = bytearray(self.pe.read(base + at, len(body)))
            for r in section.relocations:
                body[r.offset : r.offset + 4] = there[r.offset : r.offset + 4] = bytes(4)
            if body != there:
                return False
        return True

    def placements(self, name, low, high):
        data_at = self.cfg.raw.get("data_at", {})
        data_head = self.cfg.raw.get("data_head", {})
        out, weak = {}, set()
        for source, obj in self.layouts.items():
            offsets, cursor = section_layout(obj, name, self.dropped.get(source, frozenset()))
            anchors = self.anchors.get(source, {})
            bases = {anchors[n] - o for n, o in offsets.items() if anchors.get(n) is not None}
            if bases and name != ".bss":
                last = max(n for n in offsets if anchors.get(n) is not None)
                cursor = offsets[last] + obj.section(last).size
                offsets = {n: o for n, o in offsets.items() if n <= last}
            if not bases and name in data_at.get(source, {}):
                bases = {int(data_at[source][name], 16)}
                if name + "_end" in data_at[source]:
                    size = int(data_at[source][name + "_end"], 16) - next(iter(bases))
                    offsets = {n: o for n, o in offsets.items() if o + obj.section(n).size <= size}
                    cursor = max((o + obj.section(n).size for n, o in offsets.items()), default=0)
            if not bases and cursor >= 8 and name != ".bss":
                image, mask = bytearray(cursor), bytearray(cursor)
                for number, at in offsets.items():
                    section = obj.section(number)
                    image[at : at + len(section.data)] = section.data
                    mask[at : at + len(section.data)] = b"\1" * len(section.data)
                    for r in section.relocations:
                        mask[at + r.offset : at + r.offset + 4] = bytes(4)
                where = self.section[name]
                haystack = self.pe.data[
                    where.file_offset : where.file_offset + min(where.file_size, where.virtual_size)
                ]
                hits = find_masked(haystack, where.virtual_address, bytes(image), bytes(mask), 6)
                if len(hits) == 1 and any(image[slice(*longest_run(mask))]):
                    bases = {hits[0]}
                    weak.add(self.unit_index[source])
            if len(bases) == 1 and cursor:
                base = bases.pop()
                if low <= base and base + cursor <= high and (name == ".bss" or self.same_bytes(base, offsets, obj)):
                    head = data_head.get(source, {}).get(name)
                    if head is not None and low <= int(head, 16) <= base:
                        out[self.unit_index[source]] = (int(head, 16), base + cursor - int(head, 16))
                    else:
                        out[self.unit_index[source]] = (base, cursor)
        items = sorted((start, unit, size) for unit, (start, size) in out.items())
        best = []
        for k, (start, unit, size) in enumerate(items):
            weight = size if unit in weak else (size + 1) << 20
            options = [
                (best[j][0] + weight, j) for j in range(k) if items[j][1] < unit and items[j][0] + items[j][2] <= start
            ]
            best.append(max(options, default=(weight, None)))
        k = max(range(len(best)), key=lambda i: best[i][0], default=None)
        kept = {}
        while k is not None:
            start, unit, size = items[k]
            kept[unit] = (start, size)
            k = best[k][1]
        return kept

    def section_base(self, source, obj, number):
        base = self.anchors.get(source, {}).get(number)
        if base is not None or number is None:
            return base
        name = obj.section(number).name.split("$")[0]
        offsets, _ = section_layout(obj, name, self.dropped.get(source, frozenset()))
        if number not in offsets:
            return None
        anchored = [n for n in offsets if n in self.anchors.get(source, {})]
        if anchored:
            bases = {self.anchors[source][n] - offsets[n] for n in anchored}
            if len(bases) != 1 or offsets[number] > max(offsets[n] for n in anchored):
                return None
            return bases.pop() + offsets[number]
        at = self.cfg.raw.get("data_at", {}).get(source, {}).get(name)
        if at is not None:
            return int(at, 16) + offsets[number]
        if self.cfg.linked(source) and name in self.ranges.get(source, {}):
            return self.ranges[source][name][0] + offsets[number]
        return None

    def owners(self, name, low, high, placed, data_owner=None):
        votes = defaultdict(lambda: defaultdict(float))
        if data_owner:
            for at in self.fixups:
                if low <= self.value_at[at] < high and self.section_of(at) == ".data":
                    unit = data_owner(at)
                    if unit is not None:
                        votes[self.value_at[at]][unit] += 1
        for unit, (start, size) in placed.items():
            votes[start][unit] += 1000
            votes[start + size - 1][unit] += 1000
        links = []
        for at in self.fixups:
            value = self.value_at[at]
            where = self.section_of(at)
            if low <= value < high:
                if where == ".text":
                    votes[value][self.unit_at_code(at)] += 1
                elif where == name:
                    links.append((at, value))
            if where == name and low <= at < high and self.section_of(value) == ".text":
                unit = self.unit_at_code(value)
                if unit is not None:
                    votes[at][unit] += 0.5
        direct = {address: {u: w for u, w in v.items() if w < 1000} for address, v in votes.items()}
        for at, value in links:
            for unit, weight in direct.get(value, {}).items():
                votes[at][unit] += weight / 2
            for unit, weight in direct.get(at, {}).items():
                votes[value][unit] += weight / 2
        out = []
        for address, v in sorted(votes.items()):
            if not v:
                continue
            spread = len([u for u, w in v.items() if w < 1000])
            out.append((address, {u: (w if w >= 1000 else w / max(spread, 1)) for u, w in v.items()}))
        return out

    def data_ranges(self):
        data = self.section[".data"]
        low, high = data.virtual_address, data.virtual_address + data.virtual_size
        self.head = []
        if re.fullmatch(rb"\d\d:\d\d:\d\d", self.pe.read(low, 8)):
            self.head.append((BUILD_STAMP[0], low, low + BUILD_STAMP[1]))
            low += BUILD_STAMP[1]
        placed = self.placements(".data", low, high)
        self.placed_by = {".data": placed}
        chosen, _ = monotone(self.owners(".data", low, high, placed), len(self.unit_names))
        spans = boundaries(chosen, self.unit_names, low, high, self.fixup_set, placed)
        self.start_at_placements(spans, placed, ".data")
        for unit, (start, end) in spans.items():
            self.ranges[unit][".data"] = (start, end, min(8, start & -start))
        for unit, start, end in self.head:
            self.ranges[unit][".data"] = (start, end, 8)

    def start_at_placements(self, spans, placed, name):
        firsts = {self.unit_names[index]: at for index, (at, _) in placed.items()}
        for source, obj in self.layouts.items():
            offsets, _ = section_layout(obj, name, self.dropped.get(source, frozenset()))
            first = next((n for n, o in offsets.items() if o == 0), None)
            if source not in firsts and first in self.anchors.get(source, {}):
                firsts[source] = self.anchors[source][first]
        ends = {end: unit for unit, (_, end) in spans.items()}
        for unit, at in firsts.items():
            start, end = spans.get(unit, (at, at))
            if not start < at < start + 8 or start not in ends:
                continue
            if name != ".bss" and any(self.pe.read(start, at - start)):
                continue
            before = ends.pop(start)
            spans[before] = (spans[before][0], at)
            spans[unit] = (at, end)
            ends[at] = before

    def bss_ranges(self):
        bss = self.section[".bss"]
        low, high = bss.virtual_address, bss.virtual_address + bss.virtual_size
        placed = self.placements(".bss", low, high)
        self.placed_by[".bss"] = placed
        data_spans = sorted(
            (start, end, self.unit_index[unit])
            for unit, keys in self.ranges.items()
            for key, (start, end, _) in keys.items()
            if key == ".data" and unit in self.unit_index
        )

        def data_owner(address):
            i = bisect_right(data_spans, (address, float("inf"), 0)) - 1
            return data_spans[i][2] if i >= 0 and data_spans[i][0] <= address < data_spans[i][1] else None

        items = self.owners(".bss", low, high, placed, data_owner)
        _, prefixes = monotone(items, len(self.unit_names))
        cut = max(range(len(items) + 1), key=lambda k: 2 * (prefixes[k - 1] if k else 0) - k)
        self.common = items[cut][0] - items[cut][0] % 8 if cut < len(items) else high
        chosen, _ = monotone(items[:cut], len(self.unit_names))
        referenced = sorted(set(self.value_at.values()) - set(self.bindings.values()))
        spans = boundaries(chosen, self.unit_names, low, self.common, frozenset(), placed, referenced, keep_owned=True)
        self.start_at_placements(spans, placed, ".bss")
        for unit, (start, end) in spans.items():
            self.ranges[unit][".bss"] = (start, end, min(8, start & -start))
        self.ranges["common"][".bss"] = (self.common, high & ~7, 8)
        if high & 7:
            self.ranges["common"][".bss$common"] = (high & ~7, high, 1)

    def runtime_ranges(self):
        crt = self.section.get(".CRT")
        if crt:
            self.ranges["ccinit.obj"][".CRT$XCA"] = (crt.virtual_address, crt.virtual_address + 4, 4)
            self.ranges["ccinit.obj"][".CRT$XCZ"] = (crt.virtual_address + 4, crt.virtual_address + crt.virtual_size, 4)
        exc = self.section[".exc"]
        self.ranges["cexc.obj"][".exc$AAA"] = (exc.virtual_address, exc.virtual_address, 4)
        self.ranges["cexc.obj"][".exc$CCC"] = (self.exc_end, exc.virtual_address + exc.virtual_size, 4)
        tls = self.section.get(".tls")
        if tls:
            owner = (
                next(
                    (
                        u
                        for u in self.unit_names
                        if u in self.layouts
                        and any(s.name.split("$")[0] == ".tls" and s.size for s in self.layouts[u].sections)
                    ),
                    None,
                )
                or next((u for u in self.unit_names if u.endswith("file_io_g.c")), None)
                or next((u for u in self.unit_names if Path(u).stem.startswith("file_io")), None)
            )
            if owner is None:
                raise project.Error(
                    "no unit to own .tls: no compiled object has thread-local data, and no file_io unit"
                )
            self.ranges[owner][".tls"] = (tls.virtual_address, tls.virtual_address + tls.virtual_size, 4)

    def trim_padding(self):
        known = sorted(set(self.value_at.values()) | set(self.bindings.values()))
        for name in (".rdata", ".data", ".bss"):
            spans = sorted(
                (start, end, align, unit)
                for unit, keys in self.ranges.items()
                for key, (start, end, align) in keys.items()
                if key == name
            )
            for k, (start, end, align, unit) in enumerate(spans[:-1]):
                following, next_align = spans[k + 1][0], spans[k + 1][2]
                if unit not in self.layouts or following != end:
                    continue
                _, size = section_layout(self.layouts[unit], name, self.dropped.get(unit, frozenset()))
                placement = self.placed_by.get(name, {}).get(self.unit_index.get(unit))
                content = (placement[0] if placement else start) + size
                if not size or content >= end or (content + next_align - 1) // next_align * next_align != end:
                    continue
                if name != ".bss" and any(self.pe.read(content, end - content)):
                    continue
                i = bisect_right(known, content - 1)
                if i < len(known) and known[i] < end:
                    continue
                self.ranges[unit][name] = (start, content, align)

    def inexact_data_names(self, unnamed, starts, data_unit, own_names):
        out, refused = {}, set()
        for f in self.functions:
            obj = self.objects.get(f.source)
            if obj is None or f.address in self.found.get(f.source, {}):
                continue
            original = []
            for at in self.fixups_in(f.address, f.address + f.size):
                value = self.value_at[at]
                if value in unnamed and data_unit(value) == f.source and value not in original:
                    original.append(value)
            if not original:
                continue
            symbol = next(
                (s for s in obj.symbols if s.function and s.section > 0 and names.c_name(s.name) == f.name), None
            )
            if symbol is None:
                continue
            section = obj.section(symbol.section)
            end = min(
                [s.value for s in obj.symbols if s.section == symbol.section and s.function and s.value > symbol.value]
                + [section.size]
            )
            compiled = []
            for r in sorted(section.relocations, key=lambda r: r.offset):
                target = obj.by_index.get(r.symbol)
                if not symbol.value <= r.offset < end or r.kind != DIR32 or not target or target.section <= 0:
                    continue
                data = obj.section(target.section)
                if data.code:
                    continue
                offset = target.value + struct.unpack_from("<I", section.data, r.offset)[0]
                members = sorted(
                    (s.value, s.name)
                    for s in obj.symbols
                    if s.section == target.section and s.storage in (EXTERNAL, STATIC) and not s.name.startswith(".")
                )
                if data.name == ".bss":
                    if self.section_base(f.source, obj, target.section) is not None:
                        continue
                    members = self.bss_statics.get(f.source, [])
                name = next((n for v, n in members if v == offset), None)
                if name is None or name in own_names[f.source]:
                    continue
                size = min([v for v, _ in members if v > offset] + [data.size]) - offset
                if (name, size) not in compiled:
                    compiled.append((name, size))
            if len(original) != len(compiled):
                continue
            for address, (name, size) in zip(original, compiled):
                gap = starts[bisect_right(starts, address)] - address if address < starts[-1] else size
                if not size <= gap < size + 8 or out.get(address, name) != name:
                    refused.add(address)
                out[address] = name
        return {address: name for address, name in out.items() if address not in refused}

    def fixups_in(self, low, high):
        return self.sorted_fixups[
            bisect_right(self.sorted_fixups, low - 1) : bisect_right(self.sorted_fixups, high - 1)
        ]

    def symbols(self):
        symbols = []
        taken = set()
        for row in self.rows:
            symbols.append(
                (
                    row["symbol"],
                    ".text",
                    row["address"],
                    dict(type="function", size=f"0x{row['size']:X}", scope="global"),
                )
            )
            taken.add(row["symbol"])
        starts = set(self.function_starts)
        size_at = {r["address"]: r["size"] for r in self.rows}
        for name, counts in self.externals.items():
            address = max(counts, key=counts.get)
            if (
                self.section_of(address) == ".text"
                and name not in taken
                and address in starts
                and not name.startswith("__imp_")
            ):
                symbols.append(
                    (name, ".text", address, dict(type="function", size=f"0x{size_at[address]:X}", scope="global"))
                )
                taken.add(name)
        for slot, name in self.import_names.items():
            symbols.append((name, ".idata", slot, dict(type="object", size="0x4", scope="global")))
            taken.add(name)
        crt, exc = self.section.get(".CRT"), self.section[".exc"]
        if crt:
            symbols.append(("___xc_a", ".CRT", crt.virtual_address, dict(type="object", size="0x4", scope="global")))
            symbols.append(
                ("___xc_z", ".CRT", crt.virtual_address + 4, dict(type="object", size="0x4", scope="global"))
            )
        symbols.append(
            ("__PcToActionStart", ".exc", exc.virtual_address, dict(type="object", size="0x0", scope="global"))
        )
        symbols.append(("__PcToActionEnd", ".exc", self.exc_end, dict(type="object", size="0x4", scope="global")))
        taken |= {"___xc_a", "___xc_z", "__PcToActionStart", "__PcToActionEnd"}

        unit_ranges = defaultdict(list)
        for unit, keys in self.ranges.items():
            for key, (start, end, _) in keys.items():
                if key != ".bss$common" and key.split("$")[0] in DATA_SECTIONS:
                    unit_ranges[key.split("$")[0]].append((start, end, unit))
        for where in unit_ranges:
            unit_ranges[where].sort()

        def data_unit(address):
            spans = unit_ranges.get(self.section_of(address), [])
            i = bisect_right(spans, (address, float("inf"), "")) - 1
            return spans[i][2] if i >= 0 and spans[i][0] <= address < spans[i][1] else None

        extent, placed_members, claimed = {}, [], set()

        def find_own(source, body, relocations, lo, hi):
            if any(lo <= r.offset < hi for r in relocations):
                return None
            body = body.rstrip(b"\0") + b"\0"
            if len(body) < 4:
                return None
            hits = []
            for spans in unit_ranges.values():
                for start, end, unit in spans:
                    if unit != source:
                        continue
                    try:
                        chunk = self.pe.read(start, end - start)
                    except ValueError:
                        continue
                    at = chunk.find(body)
                    while at >= 0 and len(hits) < 2:
                        if at == 0 or chunk[at - 1] == 0:
                            hits.append(start + at)
                        at = chunk.find(body, at + 1)
            return hits[0] if len(hits) == 1 else None

        heads = {unit: self.objects[unit] for unit, _, _ in self.head if unit in self.objects}
        for source, obj in {**self.layouts, **heads}.items():
            for section in obj.sections:
                if section.code:
                    continue
                number = section.number
                base = self.section_base(source, obj, number)
                members = sorted(
                    (s.value, s.name, s.storage)
                    for s in obj.symbols
                    if s.section == number and s.storage in (EXTERNAL, STATIC) and not s.name.startswith((".", "$"))
                )
                data, mask = section.data, None
                if data:
                    mask = bytearray(b"\1" * len(data))
                    for r in section.relocations:
                        mask[r.offset : r.offset + 4] = bytes(4)

                def agrees(lo, hi, at=None):
                    at = base if at is None else at
                    if mask is None or at is None:
                        return False
                    try:
                        there = self.pe.read(at + lo, hi - lo)
                    except ValueError:
                        return False
                    return len(there) == hi - lo and all(not mask[i] or there[i - lo] == data[i] for i in range(lo, hi))

                for k, (value, name, storage) in enumerate(members):
                    following = members[k + 1][0] if k + 1 < len(members) else section.size
                    if following <= value:
                        continue
                    start = end = None
                    at = self.data_placed.get((source, number, value))
                    if (
                        at is not None
                        and data_unit(at) == source
                        and (base is None or at != base + value)
                        and (mask is None or agrees(value, following, at - value))
                    ):
                        start = at
                        body = data[value:following] if mask is not None else b""
                        end = start + (len(body.rstrip(b"\0")) + 1 if body else 1)
                    elif (
                        base is not None
                        and data_unit(base + value) == source
                        and (agrees(value, following) or self.data_placed.get((source, number, value)) == base + value)
                    ):
                        start, end = base + value, base + following
                        if (
                            mask is None
                            and k + 1 < len(members)
                            and self.data_placed.get((source, number, following)) != end
                        ):
                            end = start + 1
                    elif mask is not None:
                        start = find_own(source, data[value:following], section.relocations, value, following)
                        if start is not None:
                            end = start + len(data[value:following].rstrip(b"\0")) + 1
                    if start is None or start in claimed:
                        continue
                    claimed.add(start)
                    extent[start] = max(extent.get(start, 0), end)
                    placed_members.append((start, name, storage, source))
        names_at = defaultdict(list)
        for name, address in self.bindings.items():
            if self.section_of(address) in DATA_SECTIONS:
                names_at[address].append((0, name))
        for start, name, storage, source in placed_members:
            if storage == EXTERNAL:
                names_at[start].append((1, name))
        for name, counts in self.externals.items():
            address = max(counts, key=counts.get)
            if self.section_of(address) in DATA_SECTIONS:
                names_at[address].append((2, name))
        home = {}
        for address, entries in names_at.items():
            for priority, name in entries:
                if name in taken:
                    continue
                if name not in home or priority < home[name][0]:
                    home[name] = (priority, address)
        for address, entries in names_at.items():
            for priority, name in entries:
                if name in home and home[name][0] == 0 and priority > 0 and address != home[name][1]:
                    self.warn(
                        f"{name} is 0x{home[name][1]:08x} by bindings.json, but the compiled objects place it "
                        f"at 0x{address:08x}"
                    )
        named = defaultdict(list)
        for name, (priority, address) in home.items():
            named[address].append((priority, name))
        for address, entries in named.items():
            if len(entries) > 1:
                self.warn(f"0x{address:08x} has two names: {', '.join(n for _, n in sorted(entries))}")
        local_names = {
            start: name for start, name, storage, _ in placed_members if storage == STATIC and start not in named
        }
        for source, statics in self.bss_statics.items():
            obj = self.layouts.get(source)
            number = next((section.number for section in obj.sections if section.name == ".bss"), None) if obj else None
            base = self.section_base(source, obj, number) if obj else None
            if base is None or data_unit(base) != source:
                continue
            size = next(section.size for section in obj.sections if section.number == number)
            for k, (offset, name) in enumerate(statics):
                start = base + offset
                following = statics[k + 1][0] if k + 1 < len(statics) else size
                end = base + max(following, offset + 1)
                if start in named:
                    extent[start] = max(extent.get(start, 0), end)
                    continue
                if start in local_names:
                    continue
                local_names[start] = name
                extent[start] = max(extent.get(start, 0), end)
        reach = defaultdict(int)
        for obj in self.layouts.values():
            for s in obj.symbols:
                if s.common:
                    reach[s.name] = max(reach[s.name], s.value)
            for section in obj.sections:
                if not section.code or not section.data:
                    continue
                for r in section.relocations:
                    target = obj.by_index.get(r.symbol)
                    if r.kind == DIR32 and target and target.section == 0 and target.storage == EXTERNAL:
                        stored = struct.unpack_from("<I", section.data, r.offset)[0]
                        if 0 < stored < 0x10000:
                            reach[target.name] = max(reach[target.name], stored + 1)
        for name, (priority, address) in home.items():
            if reach.get(name) and self.section_of(address) in DATA_SECTIONS:
                extent[address] = max(extent.get(address, 0), address + reach[name])
        spans = sorted((start, end) for start, end in extent.items() if start in named or start in local_names)
        span_starts = [start for start, _ in spans]

        def inside_named(address):
            i = bisect_right(span_starts, address) - 1
            return i >= 0 and spans[i][0] < address < spans[i][1]

        referrers = defaultdict(set)
        for at in self.fixups:
            value = self.value_at[at]
            if self.section_of(value) in DATA_SECTIONS:
                referrer = self.unit_names[self.unit_at_code(at)] if self.section_of(at) == ".text" else data_unit(at)
                referrers[value].add(referrer)
        unit_starts = {start for spans_ in unit_ranges.values() for start, _, _ in spans_}
        targets = {value for value in self.value_at.values() if self.section_of(value) in DATA_SECTIONS} | unit_starts
        function_at = {row["address"]: row["name"] for row in self.rows}
        exc_names = {}
        for action, (pc, start, following) in self.exc_tables.items():
            base = f"@exc_{function_at.get(pc, f'{pc:08X}')}"
            exc_names[action] = base
            parts = sorted(t for t in targets if start <= t < following and t != action)
            for n, address in enumerate(parts, 1):
                exc_names[address] = f"{base}_{n}"
        unnamed = {
            a for a in targets if a not in named and a not in local_names and a not in exc_names and not inside_named(a)
        }
        own_names = defaultdict(set)
        for _, name, _, source in placed_members:
            own_names[source].add(name)
        paired = self.inexact_data_names(unnamed, sorted(targets | set(named) | set(local_names)), data_unit, own_names)
        for address in sorted(targets | set(named) | set(local_names)):
            where = self.section_of(address)
            owner = data_unit(address)
            if named.get(address):
                for _, name in sorted(named[address]):
                    symbols.append((name, where, address, {"scope": "global"}))
                continue
            if inside_named(address) and address not in unit_starts:
                continue
            outside = referrers.get(address, set()) - {owner}
            if len(outside) == 1 and not referrers[address] & {owner}:
                (referrer,) = outside
                placement = self.placed_by.get(where, {}).get(self.unit_index.get(referrer))
                if placement and address < placement[0] < address + 8 and address not in unit_starts:
                    continue
            if address in local_names and not outside:
                symbols.append((local_names[address], where, address, {"scope": "local"}))
                continue
            if address in paired and not outside:
                symbols.append((paired[address], where, address, {"scope": "local"}))
                continue
            symbols.append(
                (
                    exc_names.get(address, f"lbl_{address:08X}"),
                    where,
                    address,
                    {"scope": "global" if outside else "local"},
                )
            )

        by_section = defaultdict(list)
        for i, (name, where, address, options) in enumerate(symbols):
            if where in DATA_SECTIONS:
                by_section[where].append(i)
        for where, indices in by_section.items():
            indices.sort(key=lambda i: symbols[i][2])
            section = self.section[where]
            ends = sorted(
                {end for _, end, _ in unit_ranges.get(where, [])} | {section.virtual_address + section.virtual_size}
            )
            every = sorted({symbols[i][2] for i in indices})
            globals_ = sorted({symbols[i][2] for i in indices if symbols[i][3].get("scope") == "global"})
            for i in indices:
                name, _, address, options = symbols[i]
                pool = globals_ if options["scope"] == "global" and address in extent else every
                n = bisect_right(pool, address)
                following = pool[n] if n < len(pool) else ends[-1]
                n = bisect_right(ends, address)
                unit_end = ends[n] if n < len(ends) else ends[-1]
                size = max(min(following, unit_end) - address, 0)
                if address in extent and (name in home or address in local_names):
                    size = min(extent[address], unit_end) - address
                symbols[i] = (name, where, address, dict(type="object", size=f"0x{size:X}", scope=options["scope"]))
        symbols.sort(key=lambda s: (SECTION_ORDER.index(s[1]), s[2], s[0]))
        last = None
        for name, where, address, options in symbols:
            if where not in DATA_SECTIONS:
                continue
            if last and last[1] == where and address < last[2] + int(last[3]["size"], 16):
                self.warn(f"{name} (0x{address:08x}) is inside {last[0]} (0x{last[2]:08x}, size {last[3]['size']})")
            if not last or last[1] != where or address + int(options["size"], 16) > last[2] + int(last[3]["size"], 16):
                last = (name, where, address, options)
        return [
            formats.Symbol(name, where, address, list(options.items())) for name, where, address, options in symbols
        ]

    def splits(self):
        order = list(self.unit_names)
        for runtime, after in RUNTIME.items():
            order.insert(order.index(after) + 1 if after in order else 1, runtime)
        order = [unit for unit, _, _ in self.head] + order + ["common"]
        sections = {
            name: [("type", kind), ("align", str(align))] for name, kind, align in SECTIONS if name in self.section
        }
        renames = {".exc": ".exc$BBB", ".tls": ".tls$"}
        defaults = {name: align for name, _, align in SECTIONS}
        units = []
        for unit in order:
            if not self.ranges.get(unit):
                continue
            ranges = []
            for key in sorted(self.ranges[unit], key=lambda k: (SECTION_ORDER.index(k.split("$")[0]), k)):
                start, end, align = self.ranges[unit][key]
                base = key.split("$")[0]
                options = []
                if key == ".bss$common":
                    options = [("common", "1")]
                else:
                    if align < defaults[base]:
                        options.append(("align", str(align)))
                    if "$" in key:
                        options.append(("rename", key))
                    elif key in renames:
                        options.append(("rename", renames[key]))
                ranges.append(formats.Range(base, start, end, options))
            units.append(formats.Unit(unit, ranges))
        return formats.Splits(sections, units)

    def run(self):
        self.gather_evidence()
        self.name_functions()
        self.text_units()
        self.exception_ranges()
        self.data_ranges()
        self.bss_ranges()
        self.runtime_ranges()
        self.trim_padding()
        return self.splits(), self.symbols()


def main():
    parser = argparse.ArgumentParser(
        description="Write config/VERSION/splits.txt and symbols.txt, the original's units and symbols.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("versions", nargs="*", metavar="VERSION", help="default: every version")
    parser.add_argument("--check", action="store_true", help="write nothing; fail if a file would change")
    parser.add_argument("--stamp", metavar="FILE", help="with --check: touch FILE when nothing would change (ninja)")
    args = parser.parse_args()
    try:
        versions = [project.version(v, original=True) for v in args.versions] or list(project.VERSIONS)
        if not args.check:
            targets = [f"build/{v}/compare.json" for v in versions] + [
                str(project.relative(build.object_path(v, c.source, debug=True)))
                for v in versions
                for c in build.compilations(v)
                if not c.pro4 and build.object_path(v, c.source).exists()
            ]
            result = subprocess.run(["ninja", *targets], cwd=project.ROOT, capture_output=True, text=True)
            if result.returncode:
                raise project.Error(
                    f"ninja (the objects with symbols on) failed:\n{(result.stdout + result.stderr)[-3000:]}"
                )
        tx = project.Transaction()
        for v in versions:
            layout = Layout(v)
            splits, symbols = layout.run()
            tx.write(v.config / "splits.txt", formats.format_splits(splits))
            tx.write(v.config / "symbols.txt", formats.format_symbols(symbols))
            if not args.check:
                for warning in layout.warnings:
                    print(f"{v}: warning: {warning}")
                print(f"{v}: {len(splits.units)} units, {len(symbols)} symbols; COMMON from 0x{layout.common:08x}")
        changed = tx.changes()
        if args.check:
            for path in sorted(changed):
                print(f"{project.relative(path)} is out of date: run tools/layout.py")
            if changed:
                sys.exit(1)
            if args.stamp:
                project.write_atomic(args.stamp, "")
            return
        for path in tx.commit():
            print(f"wrote {project.relative(path)}")
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
