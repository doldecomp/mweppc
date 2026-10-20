#!/usr/bin/env python3

import argparse
import bisect
import collections
import difflib
import json
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import config as mwconfig
from mw import project
from mw.pe import PEFile

CONFIG = project.ROOT / "config"

K = 16


class Image:
    def __init__(self, path):
        self.pe = PEFile(project.ROOT / path)
        text = next(s for s in self.pe.sections if s.name == ".text")
        self.start = text.virtual_address
        data = self.pe.data[text.file_offset : text.file_offset + min(text.virtual_size, text.file_size)]
        self.text = data
        self.end = self.start + len(data)
        self.relocs = set(self.pe.base_relocations)
        masked = bytearray(data)
        for address in self.relocs:
            if self.start <= address < self.end - 3:
                masked[address - self.start : address - self.start + 4] = bytes(4)
        i = 0
        while i < len(masked) - 4:
            if masked[i] in (0xE8, 0xE9):
                masked[i + 1 : i + 5] = bytes(4)
                i += 5
            else:
                i += 1
        self.masked = bytes(masked)

    def references(self, address, size):
        refs = {}
        for a in range(address, address + size - 3):
            if a in self.relocs:
                refs[a - address] = ("abs", struct.unpack_from("<I", self.text, a - self.start)[0])
        i = address - self.start
        end = i + size
        while i < end - 4:
            if self.masked[i] in (0xE8, 0xE9) and self.text[i] in (0xE8, 0xE9):
                rel = struct.unpack_from("<i", self.text, i + 1)[0]
                refs[i + 1 - (address - self.start)] = ("rel", self.start + i + 5 + rel)
                i += 5
            else:
                i += 1
        return refs

    def call_targets(self):
        targets = set()
        for i in range(len(self.text) - 5):
            if self.text[i] == 0xE8:
                displacement = struct.unpack_from("<i", self.text, i + 1)[0]
                if displacement == 0:
                    continue
                target = self.start + i + 5 + displacement
                if self.start < target < self.end and target % 16 == 0:
                    before = self.text[target - self.start - 3 : target - self.start]
                    if before[2] in (0x00, 0xC3) or before[0] == 0xC2:
                        targets.add(target)
        return targets

    def padded_starts(self):
        starts = set()
        for address in range(self.start + 16, self.end, 16):
            i = address - self.start
            if self.text[i] == 0 or self.text[i - 1] != 0:
                continue
            j = i - 1
            while j > 0 and self.text[j] == 0 and i - j < 16:
                j -= 1
            if self.text[j] == 0xC3 or self.text[j - 1] == 0xC2 or self.text[j - 2] == 0xC2:
                starts.add(address)
        return starts

    def size_to(self, address, next_start):
        end = next_start - self.start
        i = address - self.start
        while end > i + 1 and self.text[end - 1] == 0:
            end -= 1
        if end - i >= 2 and self.text[end - 2] == 0xC2:
            end += 1
        elif self.text[end - 1] == 0xC2:
            end += 2
        for fixup in range(self.start + end - 3, self.start + end):
            if fixup in self.relocs:
                end = fixup - self.start + 4
        return min(end, next_start - self.start) - i


def imports(pe):
    return {i.slot: (i.dll.upper(), i.name if i.name is not None else i.number) for i in pe.imports}


def falls_through(image, address, size):
    import capstone

    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    last, end = None, address
    for ins in md.disasm(image.text[address - image.start : address - image.start + size], address):
        if ins.address in image.relocs:
            break
        last, end = ins, ins.address + ins.size
    if last is None or last.mnemonic in ("ret", "retn", "jmp", "int3", "ud2"):
        return False
    padding = image.text[end - image.start : address + size - image.start]
    return not (last.mnemonic == "call" and padding.strip(b"\0") == b"" and padding)


def merge_fallthroughs(image, out):
    merged = [out[0]]
    for entry, kind, origin in out[1:]:
        previous, previous_kind, previous_origin = merged[-1]
        start = int(previous["address"], 16)
        if (
            (kind == "new" or "source" not in entry)
            and int(entry["address"], 16) - (start + previous["size"]) < 16
            and falls_through(image, start, previous["size"])
        ):
            previous = dict(previous, size=int(entry["address"], 16) + entry["size"] - start)
            merged[-1] = (previous, previous_kind, previous_origin)
            continue
        merged.append((entry, kind, origin))
    return merged


def _original(version) -> "Image":
    try:
        path = json.loads((CONFIG / version / "config.json").read_text())["original"]
    except FileNotFoundError:
        raise project.Error(f"config/{version}/config.json does not exist") from None
    except KeyError:
        raise project.Error(f"config/{version}/config.json names no original (a derived version)") from None
    if not (project.ROOT / path).exists():
        raise project.Error(f"{path} does not exist: run ninja first, which downloads it")
    return Image(path)


def port(source_version, target_version):
    A = _original(source_version)
    B = _original(target_version)
    for name in ("functions.json", "bindings.json", "state.json"):
        if not (CONFIG / source_version / name).exists():
            raise project.Error(f"config/{source_version}/{name} does not exist: FROM must be laid out")
    functions = sorted(
        json.loads((CONFIG / source_version / "functions.json").read_text()), key=lambda f: int(f["address"], 16)
    )

    index = collections.defaultdict(list)
    for i in range(len(B.masked) - K):
        index[B.masked[i : i + K]].append(i)

    mapped, how = {}, {}
    for f in functions:
        address, size = int(f["address"], 16), f["size"]
        if size < K:
            continue
        body = A.masked[address - A.start : address - A.start + size]
        hits = [c for c in index.get(body[:K], [])[:256] if B.masked[c : c + size] == body]
        if len(hits) == 1:
            mapped[address] = B.start + hits[0]
            how[address] = "exact"

    calls = collections.defaultdict(collections.Counter)
    addresses = collections.defaultdict(collections.Counter)
    by_address = {int(f["address"], 16): f for f in functions}
    for a, b in list(mapped.items()):
        size = by_address[a]["size"]
        ra, rb = A.references(a, size), B.references(b, size)
        for offset, (kind, target) in ra.items():
            if offset in rb and rb[offset][0] == kind:
                (calls if kind == "rel" else addresses)[target][rb[offset][1]] += 1
    claimed = set(mapped.values())
    for target, votes in calls.items():
        ((b, n),) = votes.most_common(1)
        if target in by_address and target not in mapped and b not in claimed and n * 2 > sum(votes.values()):
            mapped[target] = b
            how[target] = "call-graph"
            claimed.add(b)

    starts = sorted(B.call_targets() | B.padded_starts() | set(mapped.values()))
    order = [int(f["address"], 16) for f in functions]
    position = {a: i for i, a in enumerate(order)}
    anchors = [(a, mapped[a]) for a in order if a in mapped]
    for (a1, b1), (a2, b2) in zip(anchors, anchors[1:]):
        if b2 <= b1:
            continue
        gap = [a for a in order[position[a1] + 1 : position[a2]] if a not in mapped]
        if not gap:
            continue
        candidates = starts[bisect.bisect_right(starts, b1) : bisect.bisect_left(starts, b2)]
        if len(candidates) == len(gap):
            for a, b in zip(gap, candidates):
                mapped[a] = b
                how[a] = "order"

    estimated = {}
    for i, b in enumerate(starts):
        estimated[b] = B.size_to(b, starts[i + 1] if i + 1 < len(starts) else B.end)
    aligned = collections.defaultdict(collections.Counter)
    for a, b in mapped.items():
        if how[a] == "exact":
            continue
        size_a, size_b = by_address[a]["size"], estimated.get(b, 0)
        if not size_a or not size_b or size_a * size_b > 4_000_000:
            continue
        x = A.masked[a - A.start : a - A.start + size_a]
        y = B.masked[b - B.start : b - B.start + size_b]
        ra, rb = A.references(a, size_a), B.references(b, size_b)
        for block in difflib.SequenceMatcher(None, x, y, autojunk=False).get_matching_blocks():
            for offset, (kind, target) in ra.items():
                if kind == "abs" and block.a <= offset and offset + 4 <= block.a + block.size:
                    other = rb.get(block.b + offset - block.a)
                    if other and other[0] == "abs":
                        aligned[target][other[1]] += 1

    data = {}
    for votes_by_target in (aligned, addresses):
        for target, votes in votes_by_target.items():
            ((b, n),) = votes.most_common(1)
            if n * 2 > sum(votes.values()):
                data[target] = b
    data.update({a: b for a, b in mapped.items()})
    imports_b = {key: address for address, key in imports(B.pe).items()}
    for address, key in imports(A.pe).items():
        if key in imports_b:
            data[address] = imports_b[key]
    sections_b = {s.name: s for s in B.pe.sections}

    def by_section(address):
        for s in A.pe.sections:
            if s.virtual_address <= address < s.virtual_address + max(s.virtual_size, s.file_size) + 1:
                if s.name in sections_b and s.name not in (".text", ".data", ".rdata", ".bss"):
                    return sections_b[s.name].virtual_address + address - s.virtual_address
        return None

    inverse = {b: a for a, b in mapped.items()}
    out = []
    for i, b in enumerate(starts):
        next_start = starts[i + 1] if i + 1 < len(starts) else B.end
        if b in inverse:
            f = by_address[inverse[b]]
            size = f["size"] if how[inverse[b]] == "exact" else B.size_to(b, next_start)
            entry = dict(name=f["name"], address=f"0x{b:08x}", size=size)
            if f.get("source"):
                entry["source"] = f["source"]
            out.append((entry, how[inverse[b]], f["address"]))
        else:
            prior = [e for e, _, _ in out if int(e["address"], 16) < b]
            if prior and int(prior[-1]["address"], 16) + prior[-1]["size"] > b:
                continue
            out.append((dict(name=f"fn_{b:08x}", address=f"0x{b:08x}", size=B.size_to(b, next_start)), "new", None))

    out = merge_fallthroughs(B, out)

    bindings = json.loads((CONFIG / source_version / "bindings.json").read_text())
    bound = sorted(int(v, 0) for v in bindings.values())
    known = sorted(a for a in data if a not in mapped)

    def from_field(address):
        i = bisect.bisect_left(known, address)
        if i == len(known) or known[i] - address > 0x1000:
            return None
        field = known[i]
        j = bisect.bisect_right(bound, address)
        if j < len(bound) and bound[j] <= field:
            return None
        return data[field] - (field - address)

    def by_content(address):
        try:
            section = A.pe.section_for_address(address)
        except ValueError:
            return None
        if section.name not in (".data", ".rdata") or address - section.virtual_address >= section.file_size:
            i = bisect.bisect_left(known, address)
            if 0 < i < len(known):
                lo, hi = known[i - 1], known[i]
                if hi - lo <= 0x1000 and data[hi] - data[lo] == hi - lo:
                    return data[lo] + (address - lo)
            return None
        j = bisect.bisect_right(bound, address)
        end = min(
            bound[j] if j < len(bound) else address + 64, address + 64, section.virtual_address + section.file_size
        )
        blob = A.pe.read(address, end - address)
        if b"\0" in blob and blob.index(b"\0") >= 3:
            blob = blob[: blob.index(b"\0") + 1]
        if len(blob) < 2:
            return None
        i = bisect.bisect_left(known, address)
        for neighbour in sorted(known[max(i - 2, 0) : i + 2], key=lambda n: abs(n - address)):
            if abs(neighbour - address) > 0x800:
                continue
            predicted = data[neighbour] + (address - neighbour)
            try:
                if B.pe.read(predicted, len(blob)) == blob:
                    return predicted
            except ValueError:
                continue
        if len(blob) < 4 or len(set(blob)) < 3:
            return None
        hits = B.pe.find(blob, 3)
        return hits[0] if len(hits) == 1 else None

    ported, unported = {}, []
    for name, value in bindings.items():
        a = int(value, 0)
        if a in data:
            ported[name] = f"0x{data[a]:08x}"
        elif (b := by_section(a)) is not None:
            ported[name] = f"0x{b:08x}"
        elif (b := from_field(a)) is not None:
            ported[name] = f"0x{b:08x}"
        elif (b := by_content(a)) is not None:
            ported[name] = f"0x{b:08x}"
        else:
            unported.append(name)
    return dict(
        functions=out,
        bindings=ported,
        unported=unported,
        how=how,
        total=len(functions),
        monotonic=sum(1 for (_, b1), (_, b2) in zip(anchors, anchors[1:]) if b2 > b1),
        anchors=len(anchors),
        matching=json.loads((CONFIG / source_version / "state.json").read_text())["matching"],
        A=A,
        B=B,
    )


def main():
    parser = argparse.ArgumentParser(
        prog="tools/port_version.py",
        usage="%(prog)s FROM TO [--write [--force]]",
        description="Lay out TO's executable from FROM's config.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("source", metavar="FROM", help="the laid-out version (a directory of config/)")
    parser.add_argument("target", metavar="TO", help="the version to lay out")
    parser.add_argument("--write", action="store_true", help="write TO's configuration")
    parser.add_argument("--force", action="store_true", help="with --write: replace a layout TO has already")
    args = parser.parse_args()
    try:
        result = port(args.source, args.target)
    except project.Error as e:
        sys.exit(f"error: {e}")
    kinds = collections.Counter(result["how"].values())
    total = result["total"]
    mapped = sum(kinds.values())
    new = [e for e, k, _ in result["functions"] if k == "new"]
    print(
        f"{args.source} -> {args.target}: {mapped}/{total} functions mapped ({100 * mapped / total:.1f}%): "
        + ", ".join(f"{k} {v}" for k, v in kinds.most_common())
    )
    print(f"  link order kept by {result['monotonic']}/{result['anchors'] - 1} consecutive exact pairs")
    print(
        f"  {len(new)} functions of {args.target} without a {args.source} counterpart "
        f"({sum(e['size'] for e in new)} bytes)"
    )
    print(f"  bindings: {len(result['bindings'])} ported, {len(result['unported'])} not")
    if not args.write:
        return
    out = CONFIG / args.target
    if (out / "functions.json").exists() and not args.force:
        sys.exit(f"config/{args.target}/functions.json exists (corrected since its port, perhaps): --force replaces it")
    sources = collections.defaultdict(list)
    for entry, kind, _ in result["functions"]:
        if "source" in entry:
            sources[entry["source"]].append(kind)
    with project.Transaction() as tx:
        if result["matching"] is not None:
            tx.write(
                out / "state.json",
                mwconfig.dump_state(
                    [s for s in result["matching"] if sources.get(s) and all(k == "exact" for k in sources[s])], {}
                ),
            )
        tx.write(out / "functions.json", mwconfig.dump_json([e for e, _, _ in result["functions"]]))
        tx.write(out / "bindings.json", mwconfig.dump_json(dict(sorted(result["bindings"].items()))))
    project.write_atomic(
        project.ROOT / f"build/{args.target}/port.json",
        json.dumps([dict(e, how=k, **({"from": f} if f else {})) for e, k, f in result["functions"]], indent=1) + "\n",
    )


if __name__ == "__main__":
    main()
