#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from mw import config, project
from mw.pe import original


def runs_into_next(version):
    pe = original(version)
    fixups = set(pe.base_relocations)
    rows = config.functions(version)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    out = []
    for row, following in zip(rows, rows[1:]):
        if following.address - row.end > 15:
            continue
        last, end = None, row.address
        for ins in md.disasm(pe.read(row.address, row.size), row.address):
            if ins.address in fixups:
                break
            last, end = ins, ins.address + ins.size
        if (
            last is None
            or last.mnemonic in ("ret", "retn", "jmp", "int3", "ud2")
            or (last.mnemonic == "call" and last.op_str == f"0x{row.address:x}")
        ):
            continue
        if last.mnemonic == "call" and not pe.read(end, row.end - end).strip(b"\0"):
            continue
        out.append(
            config.Problem(
                project.relative(project.version(version).config / "functions.json"),
                f"{row.name} (0x{row.address:08x}) ends in {last.mnemonic} {last.op_str}: its code "
                f"runs into {following.name}",
            )
        )
    return out


def main():
    parser = argparse.ArgumentParser(
        description="Check the configuration's invariants (default: every version); exits 1 listing the problems."
    )
    parser.add_argument("--stamp", metavar="FILE", help="write FILE when there are no problems")
    parser.add_argument("versions", nargs="*", metavar="VERSION")
    args = parser.parse_args()
    stamp = args.stamp
    try:
        versions = [project.version(v, original=True) for v in args.versions] or list(project.VERSIONS)
        found = [p for v in versions for p in config.problems(v) + runs_into_next(v)]
    except project.Error as e:
        sys.exit(f"error: {e}")
    for p in found:
        print(p)
    if found:
        sys.exit(1)
    if stamp:
        project.write_atomic(stamp, "")


if __name__ == "__main__":
    main()
