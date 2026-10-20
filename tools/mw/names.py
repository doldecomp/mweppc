from __future__ import annotations

import re

_STDCALL = re.compile(r"_(?P<name>[A-Za-z_$][\w$]*)@(?P<bytes>\d+)")
_FASTCALL = re.compile(r"@(?P<name>[A-Za-z_$][\w$]*)@(?P<bytes>\d+)")


def is_mangled(name: str) -> bool:
    return name.startswith("?")


def is_import(linker_name: str) -> bool:
    return linker_name.startswith("__imp_")


def c_name(linker_name: str) -> str:
    name = linker_name
    name = name.removeprefix("__imp_")
    if is_mangled(name):
        return name
    if m := _STDCALL.fullmatch(name) or _FASTCALL.fullmatch(name):
        return m["name"]
    if name.startswith("_"):
        return name[1:]
    return name
