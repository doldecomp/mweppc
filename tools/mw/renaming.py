"""Renaming what the repository names: functions and variables (rows, bindings, symbols.txt and the code) and units.

Every rename is one transaction (project.Transaction) over every version's configuration and every source: it is made
everywhere or nowhere. A set of renames is applied at once, so chains and swaps (A -> B, B -> A) need no temporaries.

In the code a name is renamed as an identifier: not inside string or character literals, not where it is a struct member
(after `.` or `->`, or declared inside a struct or union body: a member may share a function's name, as `expression`
does; a member's type there is renamed), but in comments. Before anything is written, each new name is checked against
what it already means: another row, a binding at another address, or an identifier in the code (refused unless forced: a
local, a field or another unit's static of that name would silently change meaning).
"""

from __future__ import annotations

import json
import re
from collections import defaultdict
from dataclasses import dataclass, field
from pathlib import Path

from . import config, formats
from .names import c_name, is_import, is_mangled
from .project import ROOT, VERSIONS, Transaction, relative

CODE_SUFFIXES = (".c", ".cpp", ".h", ".opt", ".r")

_TOKEN = re.compile(
    r"""
    (?P<comment>//[^\n]*|/\*.*?(?:\*/|\Z))
  | (?P<literal>"(?:\\.|[^"\\\n])*"?|'(?:\\.|[^'\\\n])*'?)
  | (?P<number>\.?\d(?:[eEpP][+-]|[\w.])*)
  | (?P<ident>[A-Za-z_]\w*)
  | (?P<arrow>->)
  | (?P<space>\s+)
  | (?P<other>.)
""",
    re.DOTALL | re.VERBOSE,
)
_GAP = re.compile(r"(?:\s+|//[^\n]*|/\*.*?(?:\*/|\Z))*", re.DOTALL)  # what may separate two tokens


@dataclass
class Site:
    """An identifier in a source."""

    path: Path
    line: int
    start: int  # offset in the text
    end: int
    kind: str  # "code", "comment", "member" (after . or ->) or "record" (inside a struct/union body)


def identifiers(path: Path, text: str, names: set[str]):
    """The Sites of NAMES in TEXT. Comments are scanned word by word (a name in a comment is renamed too)."""
    line = 1
    previous = ""  # the last significant token
    stack: list[str] = []  # "record" or "block" per open brace
    record_pending = False
    for m in _TOKEN.finditer(text):
        kind, token = m.lastgroup, m[0]
        if kind == "comment":
            for word in re.finditer(r"[A-Za-z_]\w*", token):
                if word[0] in names:
                    yield Site(
                        path,
                        line + token.count("\n", 0, word.start()),
                        m.start() + word.start(),
                        m.start() + word.end(),
                        "comment",
                    )
        elif kind == "ident":
            if token in names:
                where = "member" if previous in (".", "->") else "record" if stack and stack[-1] == "record" else "code"
                # (in a record body, a name followed by a declarator is the member's type, not the member)
                if where == "record" and re.match(r"[A-Za-z_*]", text[_GAP.match(text, m.end()).end() :]):
                    where = "code"
                yield Site(path, line, m.start(), m.end(), where)
            if token in ("struct", "union"):
                record_pending = True
            elif not (record_pending and previous in ("struct", "union")):
                record_pending = False
        elif kind == "other" and token == "{":
            stack.append("record" if record_pending else "block")
            record_pending = False
        elif kind == "other" and token == "}":
            if stack:
                stack.pop()
        elif kind not in ("space", "comment"):
            record_pending = False
        line += token.count("\n")
        if kind not in ("space", "comment"):
            previous = token


def code_files() -> list[Path]:
    return sorted(
        p for d in ("src", "include") for p in (ROOT / d).rglob("*") if p.suffix in CODE_SUFFIXES and p.is_file()
    )


def read_code(tx: Transaction, path: Path) -> str:
    return tx.read_bytes(path).decode("latin-1")


# ------------------------------------------------------------------------------------------------------ the names


def respell(linker_name: str, mapping: dict[str, str]) -> str | None:
    """LINKER_NAME with its C name renamed by MAPPING (its decoration kept), or None when MAPPING does not rename it.
    Import slots keep the DLL's names."""
    if is_import(linker_name):
        return None
    if is_mangled(linker_name):
        return mapping.get(linker_name)
    name = c_name(linker_name)
    if name not in mapping:
        return None
    prefix_end = linker_name.index(name)
    return linker_name[:prefix_end] + mapping[name] + linker_name[prefix_end + len(name) :]


@dataclass
class Plan:
    """What a rename changes, staged in a transaction (committed by the caller)."""

    tx: Transaction = field(default_factory=Transaction)
    changes: list[str] = field(default_factory=list)  # one line per change, for the report
    problems: list[str] = field(default_factory=list)  # refusals: nothing is written while there are any
    left: list[Site] = field(default_factory=list)  # occurrences deliberately not renamed (members)


def rename(
    mapping: dict[str, str], code: bool = True, force: set[str] = frozenset(), source: str | None = None
) -> Plan:
    """Stage renaming each OLD to NEW of MAPPING everywhere (see the module). FORCE: new names to apply although the
    code already uses them. SOURCE: also assign the renamed rows to it."""
    plan = Plan()
    tx = plan.tx
    for old, new in mapping.items():
        for name in (old, new):
            if not (re.fullmatch(r"[A-Za-z_]\w*", name) or is_mangled(name)):
                plan.problems.append(f"{name!r} is not a C identifier or a mangled name")
        if is_mangled(old) != is_mangled(new):
            plan.problems.append(f"{old} -> {new}: a mangled name is renamed only to another")
    if len(set(mapping.values())) != len(mapping):
        plan.problems.append("two names are renamed to one")
    if plan.problems:
        return plan
    if source is not None and source not in config.sources():
        plan.problems.append(f"{source} is not in config/sources.json")

    for v in VERSIONS:
        fpath, bpath, spath = v.config / "functions.json", v.config / "bindings.json", v.config / "symbols.txt"
        rows = config.functions(v, tx.read(fpath))
        names = {r.name for r in rows}
        done = []
        for row in rows:
            if row.name in mapping:
                new = mapping[row.name]
                if new in names and new not in mapping:
                    plan.problems.append(f"{v}: {new} already names another row")
                done.append(f"{row.name} -> {new}")
                row.name = new
                if source is not None:
                    row.source = source
        if done:
            tx.write(fpath, config.dump_functions(rows))
            plan.changes.append(f"{relative(fpath)}: {', '.join(done)}")
            # (a review decision that gave the row its old name gives it the new one: a rename by hand overrides the
            # decision, which tools/evidence.py apply would otherwise replay)
            rpath = v.config / "name_review.json"
            if tx.read_bytes(rpath) is not None:
                text = tx.read(rpath)
                review = json.loads(text)
                renamed_at = {config.format_address(r.address): r.name for r in rows}
                count = 0
                for address, decision in review.get("functions", {}).items():
                    if decision.get("name") in mapping and renamed_at.get(address) == mapping[decision["name"]]:
                        decision["name"] = mapping[decision["name"]]
                        count += 1
                if count:
                    tx.write(rpath, json.dumps(review, indent=2) + "\n")
                    plan.changes.append(f"{relative(rpath)}: {count} decisions")

        table = config.bindings(v, tx.read(bpath))
        renamed: dict[str, set[int]] = defaultdict(set)
        done = []
        for name, address in table.items():
            new = respell(name, mapping)
            if new is not None:
                done.append(f"{name} -> {new}")
            renamed[new or name].add(address)
        for name, addresses in renamed.items():
            if len(addresses) > 1:
                plan.problems.append(
                    f"{v}: {name} would be bound at " + ", ".join(f"0x{a:08x}" for a in sorted(addresses))
                )
        if done:
            tx.write(bpath, config.dump_bindings({n: min(a) for n, a in renamed.items()}))
            plan.changes.append(f"{relative(bpath)}: {', '.join(done)}")

        if tx.read_bytes(spath) is not None:
            symbols = formats.parse_symbols(tx.read(spath), relative(spath))
            count = 0
            for s in symbols:
                new = respell(s.name, mapping)
                if new is not None:
                    s.name = new
                    count += 1
            if count:
                tx.write(spath, formats.format_symbols(symbols))
                plan.changes.append(f"{relative(spath)}: {count} names")

    if not code:
        return plan
    plain = {old: new for old, new in mapping.items() if not is_mangled(old)}
    if not plain:
        return plan
    texts = scan(tx, set(plain) | set(plain.values()))
    for new, taken in taken_names(plain, texts).items():
        if new not in force:
            where = ", ".join(f"{relative(s.path)}:{s.line}" for s in taken[:5])
            plan.problems.append(
                f"{new} already means something in the code ({len(taken)} uses: {where}"
                f"{' ...' if len(taken) > 5 else ''}); --force {new} once it is ready"
            )
    for path, (text, sites) in texts.items():
        out, cursor, count = [], 0, 0
        for s in sites:
            name = text[s.start : s.end]
            if name not in plain:
                continue
            if s.kind in ("member", "record"):
                plan.left.append(s)
                continue
            out += [text[cursor : s.start], plain[name]]
            cursor, count = s.end, count + 1
        if not count:
            continue
        new_text = "".join(out) + text[cursor:]
        # (an alias `#define NEW OLD` has become `#define NEW NEW`: delete it)
        new_text = re.sub(r"(?m)^[ \t]*#[ \t]*define[ \t]+(\w+)[ \t]+\1[ \t]*(?://[^\n]*)?\n", "", new_text)
        tx.write(path, new_text.encode("latin-1"))
        plan.changes.append(f"{relative(path)}: {count} uses")
    return plan


def scan(tx: Transaction, names: set[str]) -> dict[Path, tuple[str, list[Site]]]:
    """{path: (text, the Sites of NAMES in it)} of the code files that mention any of NAMES."""
    out = {}
    for path in code_files():
        text = read_code(tx, path)
        if any(n in text for n in names):
            sites = list(identifiers(path, text, names))
            if sites:
                out[path] = (text, sites)
    return out


def taken_names(mapping: dict[str, str], texts: dict[Path, tuple[str, list[Site]]]) -> dict[str, list[Site]]:
    """{new: its uses in the code} of MAPPING's new names that already mean something there (an identifier outside
    comments and member positions), not counting a new name that is itself renamed away, or an alias `#define NEW
    OLD` (which the rename deletes)."""
    news = {new: old for old, new in mapping.items() if new not in mapping}
    out: dict[str, list[Site]] = defaultdict(list)
    for path, (text, sites) in texts.items():
        for s in sites:
            name = text[s.start : s.end]
            if s.kind != "code" or name not in news:
                continue
            line_start = text.rfind("\n", 0, s.start) + 1
            line_end = text.find("\n", s.start)
            line = text[line_start : line_end if line_end >= 0 else len(text)]
            if re.fullmatch(rf"\s*#\s*define\s+{re.escape(name)}\s+{re.escape(news[name])}\s*(//.*)?", line):
                continue
            out[name].append(s)
    return dict(out)


# ------------------------------------------------------------------------------------------------------- units


def rename_unit(old: str, new: str) -> Plan:
    """Stage renaming the source OLD to NEW (both repository-relative): the file, config/sources.json, every version's
    rows, config.json, state.json, splits.txt and name review, and `#include "OLD"` in the code."""
    plan = Plan()
    tx = plan.tx
    if not (ROOT / old).is_file():
        plan.problems.append(f"{old} does not exist")
    if (ROOT / new).exists():
        plan.problems.append(f"{new} exists")
    table = config.sources(tx.read("config/sources.json"))
    if old not in table:
        plan.problems.append(f"{old} is not in config/sources.json")
    if plan.problems:
        return plan

    def swap(value):
        return new if value == old else value

    raw = json.loads(tx.read("config/sources.json"))
    tx.write("config/sources.json", config.dump_json({swap(k): v for k, v in raw.items()}))
    plan.changes.append(f"config/sources.json: {old} -> {new}")
    tx.write(new, tx.read_bytes(ROOT / old))
    tx.write(old, None)
    plan.changes.append(f"{old} -> {new}")

    for v in VERSIONS:
        fpath = v.config / "functions.json"
        rows = config.functions(v, tx.read(fpath))
        count = sum(r.source == old for r in rows)
        for r in rows:
            r.source = swap(r.source)
        if count:
            tx.write(fpath, config.dump_functions(rows))
            plan.changes.append(f"{relative(fpath)}: {count} rows")
        for name in ("config.json", "state.json", "name_review.json"):
            path = v.config / name
            if tx.read_bytes(path) is None:
                continue
            text = tx.read(path)
            value = json.loads(text)
            renamed = rename_in_json(value, old, new)
            if renamed != value:
                second = text.split("\n", 2)[1]
                tx.write(path, config.dump_json(renamed, len(second) - len(second.lstrip(" "))))
                plan.changes.append(f"{relative(path)}")
        spath = v.config / "splits.txt"
        if tx.read_bytes(spath) is not None:
            splits = formats.parse_splits(tx.read(spath), relative(spath))
            if any(u.name == old for u in splits.units):
                for u in splits.units:
                    u.name = swap(u.name)
                tx.write(spath, formats.format_splits(splits))
                plan.changes.append(f"{relative(spath)}")

    old_name, new_name = Path(old).name, Path(new).name
    if old_name != new_name:
        include = re.compile(rf'(#\s*include\s*")((?:[^"]*/)?){re.escape(old_name)}"')
        for path in code_files():
            if path == ROOT / old:
                continue
            text = read_code(tx, path)
            changed = include.sub(lambda m: f'{m[1]}{m[2]}{new_name}"', text)
            if changed != text:
                tx.write(path, changed.encode("latin-1"))
                plan.changes.append(f"{relative(path)}: #include")
    return plan


def rename_in_json(value, old: str, new: str):
    """VALUE with every string (key or value) equal to OLD made NEW; lists of strings stay sorted when they were."""
    if isinstance(value, dict):
        return {(new if k == old else k): rename_in_json(v, old, new) for k, v in value.items()}
    if isinstance(value, list):
        out = [rename_in_json(v, old, new) for v in value]
        if all(isinstance(v, str) for v in value) and value == sorted(value):
            out.sort()
        return out
    return new if value == old else value
