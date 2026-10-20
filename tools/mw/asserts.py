from __future__ import annotations

import re
from dataclasses import dataclass

import capstone

from .pe import PEFile

FUNCTIONS = ("CError_Internal", "__assertion_failed")

_MD = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
_FILE = re.compile(r"[\w.+-]+\.(?:c|cp|cpp|h)")
_NUMBER = re.compile(r"0x[0-9a-f]+|\d+")


@dataclass(frozen=True)
class Site:
    index: int
    line: int
    file: str


def targets(rows) -> set[int]:
    return {r.address for r in rows if r.name in FUNCTIONS}


def sites(
    pe: PEFile, code: bytes, address: int, assert_functions: set[int], strings: dict[int, str] | None = None
) -> tuple[list, list[Site]]:
    instructions = list(_MD.disasm(code, address))
    found = []
    for i, ins in enumerate(instructions):
        if ins.mnemonic != "call" or not ins.op_str.startswith("0x") or int(ins.op_str, 16) not in assert_functions:
            continue
        pushes = [p for p in instructions[max(i - 6, 0) : i] if p.mnemonic == "push" and _NUMBER.fullmatch(p.op_str)]
        line = file = None
        for p in reversed(pushes):
            value = int(p.op_str, 0)
            if file is None and strings is not None and p.address + 1 in strings:
                text = strings[p.address + 1]
                if _FILE.fullmatch(text):
                    file = text
            elif file is None and value > pe.image_base:
                try:
                    text = pe.read(value, 64).split(b"\0", 1)[0].decode("latin-1")
                except ValueError:
                    continue
                if _FILE.fullmatch(text):
                    file = text
            elif file is not None and line is None and value < 0x10000:
                line = value
        if file and line is not None:
            found.append(Site(i, line, file))
    return instructions, found
