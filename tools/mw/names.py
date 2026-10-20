"""A function's or variable's names as the C source, the linker and the configuration spell them.

The CodeWarrior x86 compilers decorate C names the Microsoft way:

    C name      linker name           when
    foo         _foo                  cdecl functions and all variables
    foo         _foo@8                __stdcall, with the bytes of arguments
    foo         @foo@8                __fastcall
    foo         __imp__foo@8          the import-table slot of a DLL function
    foo$123     _foo$123              a function-local static (the number is the compiler's)
    ?f@@YAXXZ   ?f@@YAXXZ             C++, mangled (no underscore)

functions.json names rows by C name (or the mangled name); bindings.json, symbols.txt and objects by linker name.
A leading underscore of the C name is kept: C "__x" is linker "___x".
"""

from __future__ import annotations

import re

_STDCALL = re.compile(r"_(?P<name>[A-Za-z_$][\w$]*)@(?P<bytes>\d+)")
_FASTCALL = re.compile(r"@(?P<name>[A-Za-z_$][\w$]*)@(?P<bytes>\d+)")


def is_mangled(name: str) -> bool:
    return name.startswith("?")


def is_import(linker_name: str) -> bool:
    return linker_name.startswith("__imp_")


def c_name(linker_name: str) -> str:
    """The C name (or mangled C++ name) a linker name spells; an import slot's is its function's."""
    name = linker_name
    name = name.removeprefix("__imp_")
    if is_mangled(name):
        return name
    if m := _STDCALL.fullmatch(name) or _FASTCALL.fullmatch(name):
        return m["name"]
    if name.startswith("_"):
        return name[1:]
    return name
