"""The original's assert calls: the file name and line each pushes, which name a function's unit and, against a
reference tree that keeps the lines (Pro7's), the function itself.

An assert compiles to pushes of the line and the file name (__FILE__: the bare name, "IroLoop.c"), then a call of the
assert function: the compiler's CError_Internal, or MSL's __assertion_failed (the driver's and the optimizer's
IRO_ASSERT), which pushes the condition's text as well. An inlined function's asserts carry its own file's name (a
header's, for an inline in one) and lines.
"""

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
    index: int  # the call's instruction, in the function's instructions
    line: int
    file: str


def targets(rows) -> set[int]:
    """The addresses of the assert functions among ROWS (config.Function or verdict.Row)."""
    return {r.address for r in rows if r.name in FUNCTIONS}


def sites(
    pe: PEFile, code: bytes, address: int, assert_functions: set[int], strings: dict[int, str] | None = None
) -> tuple[list, list[Site]]:
    """(CODE's instructions (it is at ADDRESS), the assert calls among them). STRINGS: {address of an instruction's
    field: the string it refers to} for code whose literals are not at their addresses in PE (a compiled function's,
    read from its object); other pushed addresses are read from PE."""
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
