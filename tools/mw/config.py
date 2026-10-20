"""The configuration, loaded into types, written back canonically, and checked.

The files and who writes them:

    config/sources.json             hand    every source: its host compiler, flags and include directories
    config/VERSION/config.json      hand    the original executable, per-version compilers and flags, data placement
    config/VERSION/functions.json   hand    every function of the original: name, address, size, source
    config/VERSION/bindings.json    hand    linker names bound to the original's addresses (data, aliases, imports)
    config/VERSION/state.json       tools   which sources are Matching (tools/update_state.py) and which of those do
                                            not link (tools/relink.py): committed, so the relink is reproducible

Loading checks each file's shape and fails with the file and the problem; problems() checks the invariants between
them (unique names, rows that do not overlap, sources that exist ...), which tools/validate.py reports. Writers produce
the files' canonical form (two-space JSON, addresses as 0x%08x), so loading and saving an unchanged file changes nothing.
"""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path

from .project import ROOT, Error, Version, relative
from .project import version as get_version

# (the compilers ninja downloads, under build/compilers/)
HOST_COMPILERS = ("pro4", "pro5", "pro53", "pro6")


class ConfigError(Error):
    pass


def _load_json(path: Path, text: str | None = None):
    try:
        return json.loads(path.read_text() if text is None else text)
    except FileNotFoundError:
        raise ConfigError(f"{relative(path)} does not exist") from None
    except json.JSONDecodeError as e:
        raise ConfigError(f"{relative(path)}:{e.lineno}: {e.msg}") from None


def dump_json(value, indent: int = 2) -> str:
    return json.dumps(value, indent=indent) + "\n"


def parse_address(text, where: str) -> int:
    if not isinstance(text, str) or not text.startswith("0x"):
        raise ConfigError(f"{where}: address {text!r} is not a string of the form 0x0040a1b0")
    try:
        return int(text, 16)
    except ValueError:
        raise ConfigError(f"{where}: address {text!r} is not hexadecimal") from None


def format_address(address: int) -> str:
    return f"0x{address:08x}"


# ---------------------------------------------------------------------------------------------------- sources.json


@dataclass(frozen=True)
class Source:
    """How config/sources.json builds one source (before a version's own changes: build.py applies those)."""

    path: str
    compiler: str
    flags: tuple[str, ...]
    include_dirs: tuple[str, ...] = ()
    system_include_dirs: tuple[str, ...] = ()

    def to_json(self) -> dict:
        out = dict(compiler=self.compiler, flags=list(self.flags))
        if self.include_dirs:
            out["include_dirs"] = list(self.include_dirs)
        if self.system_include_dirs:
            out["system_include_dirs"] = list(self.system_include_dirs)
        return out


SOURCES_PATH = ROOT / "config/sources.json"


def sources(text: str | None = None) -> dict[str, Source]:
    """config/sources.json, in its order (which is the order build.ninja lists the sources in)."""
    raw = _load_json(SOURCES_PATH, text)
    if not isinstance(raw, dict):
        raise ConfigError("config/sources.json: expected an object of sources")
    out = {}
    for path, settings in raw.items():
        where = f"config/sources.json: {path}"
        if not isinstance(settings, dict):
            raise ConfigError(f"{where}: expected an object")
        unknown = set(settings) - {"compiler", "flags", "include_dirs", "system_include_dirs"}
        if unknown:
            raise ConfigError(f"{where}: unknown keys {sorted(unknown)}")
        if settings.get("compiler") not in HOST_COMPILERS:
            raise ConfigError(f"{where}: compiler {settings.get('compiler')!r} is not one of {HOST_COMPILERS}")
        for key in ("flags", "include_dirs", "system_include_dirs"):
            if not isinstance(settings.get(key, []), list) or not all(
                isinstance(x, str) for x in settings.get(key, [])
            ):
                raise ConfigError(f"{where}: {key} must be a list of strings")
        out[path] = Source(
            path,
            settings["compiler"],
            tuple(settings["flags"]),
            tuple(settings.get("include_dirs", ())),
            tuple(settings.get("system_include_dirs", ())),
        )
    return out


# ----------------------------------------------------------------------------------------------------- config.json

VERSION_KEYS = {
    # key: its JSON type
    "derived_from": str,
    "description": str,
    "original": str,
    "sha1": str,
    "compilers": dict,
    "compiler_overrides": dict,
    "flags": dict,
    "data_at": dict,
    "data_units": dict,
    "data_head": dict,
}


@dataclass
class VersionConfig:
    """config/VERSION/config.json. The raw dictionary is kept (and written back) as it is; the properties type it."""

    version: Version
    raw: dict

    @property
    def path(self) -> Path:
        return self.version.config / "config.json"

    @property
    def original(self) -> Path:
        return ROOT / self.raw["original"]

    @property
    def sha1(self) -> str:
        return self.raw["sha1"]

    @property
    def compilers(self) -> dict[str, str]:
        """{the compiler sources.json names: the one this version uses instead}."""
        return self.raw.get("compilers", {})

    @property
    def compiler_overrides(self) -> dict[str, str]:
        """{source: the compiler it is built with in this version, whatever sources.json says}."""
        return self.raw.get("compiler_overrides", {})

    @property
    def flags(self) -> dict[str, list[str]]:
        """{source: the flags it is built with in this version, in place of sources.json's}."""
        return self.raw.get("flags", {})

    @property
    def matching(self) -> list[str]:
        """The Matching sources: each of their functions is exact, and the comparison fails if one stops being."""
        return state(self.version)["matching"]

    @property
    def relink_exclude(self) -> dict[str, str]:
        """{Matching source whose object does not link into the original: why}."""
        return state(self.version)["relink_exclude"]

    def linked(self, source: str) -> bool:
        """Whether the --matching build links SOURCE's compiled object in its unit's place."""
        return source in self.matching and source not in self.relink_exclude


def version_config(version: Version | str | None = None, text: str | None = None) -> VersionConfig:
    v = get_version(version)
    path = v.config / "config.json"
    raw = _load_json(path, text)
    for key, value in raw.items():
        if key not in VERSION_KEYS:
            raise ConfigError(f"{relative(path)}: unknown key {key!r}")
        if not isinstance(value, VERSION_KEYS[key]):
            raise ConfigError(f"{relative(path)}: {key} must be a {VERSION_KEYS[key].__name__}")
    if v.base:
        # (a derived version: its base's configuration, with its own keys over it)
        return VersionConfig(v, {**version_config(v.base).raw, **raw})
    for key in ("original", "sha1"):
        if key not in raw:
            raise ConfigError(f"{relative(path)}: {key} is missing")
    return VersionConfig(v, raw)


# ------------------------------------------------------------------------------------------------------ state.json


def state(version: Version | str | None = None, text: str | None = None) -> dict:
    """config/VERSION/state.json: {"matching": [sources], "relink_exclude": {source: why}}."""
    v = get_version(version)
    path = v.config / "state.json"
    raw = _load_json(path, text)
    if (
        set(raw) != {"matching", "relink_exclude"}
        or not isinstance(raw["matching"], list)
        or not isinstance(raw["relink_exclude"], dict)
    ):
        raise ConfigError(f'{relative(path)}: expected {{"matching": [...], "relink_exclude": {{...}}}}')
    return raw


def dump_state(matching, relink_exclude) -> str:
    return dump_json(dict(matching=sorted(matching), relink_exclude=dict(sorted(relink_exclude.items()))))


# -------------------------------------------------------------------------------------------------- functions.json


@dataclass
class Function:
    """A function of the original (a row of functions.json)."""

    name: str
    address: int
    size: int
    source: str | None = None

    @property
    def end(self) -> int:
        return self.address + self.size

    def to_json(self) -> dict:
        out = dict(name=self.name, address=format_address(self.address), size=self.size)
        if self.source is not None:
            out["source"] = self.source
        return out


def functions(version: Version | str | None = None, text: str | None = None) -> list[Function]:
    """config/VERSION/functions.json's rows, in the file's order (by address)."""
    v = get_version(version)
    path = v.config / "functions.json"
    raw = _load_json(path, text)
    if not isinstance(raw, list):
        raise ConfigError(f"{relative(path)}: expected a list of functions")
    rows, name = [], relative(path)
    for i, entry in enumerate(raw):
        where = f"{name}: row {i} ({entry.get('name') if isinstance(entry, dict) else entry!r})"
        if not isinstance(entry, dict):
            raise ConfigError(f"{where}: not an object")
        unknown = set(entry) - {"name", "address", "size", "source"}
        if unknown:
            raise ConfigError(f"{where}: unknown keys {sorted(unknown)}")
        if not isinstance(entry.get("name"), str) or not entry["name"]:
            raise ConfigError(f"{where}: no name")
        if not isinstance(entry.get("size"), int) or entry["size"] <= 0:
            raise ConfigError(f"{where}: size must be a positive integer")
        if "source" in entry and not isinstance(entry["source"], str):
            raise ConfigError(f"{where}: source must be a string")
        rows.append(
            Function(entry["name"], parse_address(entry.get("address"), where), entry["size"], entry.get("source"))
        )
    return rows


def dump_functions(rows: list[Function]) -> str:
    return dump_json([r.to_json() for r in sorted(rows, key=lambda r: r.address)])


# --------------------------------------------------------------------------------------------------- bindings.json


def bindings(version: Version | str | None = None, text: str | None = None) -> dict[str, int]:
    """config/VERSION/bindings.json: {linker name: address}."""
    v = get_version(version)
    path = v.config / "bindings.json"
    raw = _load_json(path, text)
    if not isinstance(raw, dict):
        raise ConfigError(f"{relative(path)}: expected an object of names")
    where = relative(path)
    return {name: parse_address(value, f"{where}: {name}") for name, value in raw.items()}


def dump_bindings(table: dict[str, int]) -> str:
    return dump_json({name: format_address(table[name]) for name in sorted(table)})


# ------------------------------------------------------------------------------------------------------ invariants


@dataclass
class Problem:
    file: str
    message: str

    def __str__(self) -> str:
        return f"{self.file}: {self.message}"


def problems(version: Version | str | None = None) -> list[Problem]:
    """What is wrong in a version's configuration, beyond each file's own shape."""
    v = get_version(version)
    out: list[Problem] = []
    table = sources()
    cfg = version_config(v)
    rows = functions(v)
    binds = bindings(v)
    fpath, bpath, cpath = (relative(v.config / n) for n in ("functions.json", "bindings.json", "config.json"))

    # functions.json: unique names, sorted, no overlaps, sources that exist
    seen: dict[str, Function] = {}
    previous = None
    for row in rows:
        if row.name in seen:
            out.append(
                Problem(fpath, f"{row.name} names two rows (0x{seen[row.name].address:08x}, 0x{row.address:08x})")
            )
        seen.setdefault(row.name, row)
        if previous is not None:
            if row.address < previous.address:
                out.append(Problem(fpath, f"{row.name} (0x{row.address:08x}) is out of address order"))
            elif row.address < previous.end:
                out.append(Problem(fpath, f"{row.name} (0x{row.address:08x}) overlaps {previous.name}"))
        previous = row
        if row.source is not None and row.source not in table:
            out.append(Problem(fpath, f"{row.name}: source {row.source} is not in config/sources.json"))

    # bindings.json: one name per address and one address per function name
    by_address: dict[int, list[str]] = {}
    for name, address in binds.items():
        by_address.setdefault(address, []).append(name)
    row_at = {r.address: r for r in rows}
    from .names import c_name, is_import

    for address, names in sorted(by_address.items()):
        row = row_at.get(address)
        named = [n for n in names if not (row and c_name(n) == row.name)]
        if row is not None and named and not all(is_import(n) for n in named):
            out.append(Problem(bpath, f"{', '.join(sorted(named))} at 0x{address:08x}, the address of row {row.name}"))
        elif len(named) > 1:
            out.append(Problem(bpath, f"0x{address:08x} has two names: {', '.join(sorted(named))}"))
    for name, address in binds.items():
        row = None if is_import(name) else seen.get(c_name(name))
        if row is not None and row.address != address:
            out.append(Problem(bpath, f"{name} is bound to 0x{address:08x}, but its row is at 0x{row.address:08x}"))

    # config.json: every source it names exists
    def check_source(where, path):
        if path not in table:
            out.append(Problem(cpath, f"{where} names {path}, which is not in config/sources.json"))

    for key in ("compiler_overrides", "flags", "data_at", "data_head"):
        for path in cfg.raw.get(key, {}):
            check_source(key, path)
    for path in cfg.matching:
        check_source("state.json's matching", path)
    for path in cfg.relink_exclude:
        check_source("state.json's relink_exclude", path)
    for path, after in cfg.raw.get("data_units", {}).items():
        check_source("data_units", path)
        check_source("data_units", after)
    for path in cfg.relink_exclude:
        if path not in cfg.matching:
            out.append(
                Problem(relative(v.config / "state.json"), f"relink_exclude names {path}, which is not Matching")
            )
    for path, compiler in cfg.compiler_overrides.items():
        if compiler not in HOST_COMPILERS:
            out.append(Problem(cpath, f"compiler_overrides: {path}: {compiler!r} is not a compiler"))

    # sources.json: files that exist
    for path in table:
        if not (ROOT / path).is_file():
            out.append(Problem("config/sources.json", f"{path} does not exist"))
    return out
