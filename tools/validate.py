#!/usr/bin/env python3
"""python tools/validate.py [--stamp FILE] [VERSION...]: check the configuration's invariants (default: every version).

Each file's own shape is checked when it is loaded; this checks what holds between them (tools/mw/config.py's
problems()): unique row names, rows in address order that do not overlap, sources that exist, one name per bound
address (a row's address is its row's name), bindings of a function's name at its row's address, configuration that
names only real sources. And one thing of the rows against the original: no row's code runs into the next row (its
last instruction, decoded from its start, is a ret or a jmp; a jump table may end it): such a boundary is not a
function's, and splitting there breaks lifted code, whose functions do not keep their neighbours.

Exits 1 listing the problems. ninja runs it before each comparison (--stamp writes FILE when there are none).
"""

import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from mw import config, project
from mw.pe import original


def runs_into_next(version):
    """The rows whose last instruction falls through into the next row."""
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
            # (a jump table: addresses the loader fixes up, after the last instruction)
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
            continue  # (a call that does not return: abort, a longjmp)
        out.append(
            config.Problem(
                project.relative(project.version(version).config / "functions.json"),
                f"{row.name} (0x{row.address:08x}) ends in {last.mnemonic} {last.op_str}: its code "
                f"runs into {following.name}",
            )
        )
    return out


def main():
    if sys.argv[1:2] in (["-h"], ["--help"]):
        print(__doc__)
        return
    args = sys.argv[1:]
    stamp = None
    if args[:1] == ["--stamp"]:
        if len(args) < 2:
            sys.exit(__doc__.strip())
        stamp, args = args[1], args[2:]
    try:
        versions = [project.version(v, original=True) for v in args] or list(project.VERSIONS)
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
