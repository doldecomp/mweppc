from __future__ import annotations

import collections
import os
import re
import sys
from dataclasses import dataclass, replace

from . import build, clang
from .project import ROOT, Error, Version, relative
from .project import version as get_version

_TOP = ("TranslationUnitDecl", "LinkageSpecDecl", "NamespaceDecl")
_FUNCTIONS = ("FunctionDecl", "CXXMethodDecl", "CXXConstructorDecl", "CXXDestructorDecl", "CXXConversionDecl")
_STATELESS_PRAGMAS = {"once", "mark", "message"}


@dataclass(frozen=True)
class Local:
    start: int
    end: int
    movable: bool


@dataclass(frozen=True)
class Declaration:
    kind: str
    name: str
    mangled: str
    file: str
    start: int
    end: int
    at: int
    storage: str
    inline: bool
    body: int | None
    locals: tuple[Local, ...] = ()
    shared: bool = False

    @property
    def defined(self) -> bool:
        return self.body is not None


@dataclass(frozen=True)
class Directive:
    start: int
    end: int
    kind: str
    text: str


@dataclass(frozen=True)
class Include:
    directive: Directive
    target: str
    conditional: bool
    late: bool


def _locate(tree: dict) -> dict[int, tuple]:
    current = [None]
    spans = {}

    def location(loc):
        if not loc:
            return None
        if "expansionLoc" in loc:
            location(loc.get("spellingLoc"))
            return location(loc["expansionLoc"])
        if "file" in loc:
            current[0] = loc["file"]
        return (current[0], loc["offset"], loc.get("tokLen", 0)) if "offset" in loc else None

    def visit(node):
        if isinstance(node, list):
            for item in node:
                visit(item)
            return
        if "offset" in node or "expansionLoc" in node:
            location(node)
            return
        at = begin = end = None
        for key, value in node.items():
            if key == "loc":
                at = location(value)
            elif key == "range":
                begin, end = location(value.get("begin")), location(value.get("end"))
            elif isinstance(value, (dict, list)):
                visit(value)
        if "kind" in node:
            spans[id(node)] = (at, begin, end)

    limit = sys.getrecursionlimit()
    sys.setrecursionlimit(max(limit, 100000))
    try:
        visit(tree)
    finally:
        sys.setrecursionlimit(limit)
    return spans


def _calls(node) -> bool:
    if isinstance(node, dict):
        if node.get("kind") in ("CallExpr", "CXXConstructExpr", "CXXMemberCallExpr", "CXXOperatorCallExpr"):
            return True
        return any(_calls(v) for v in node.values() if isinstance(v, (dict, list)))
    if isinstance(node, list):
        return any(_calls(v) for v in node)
    return False


def _locals(body: dict, spans: dict) -> tuple[Local, ...]:
    out = []
    for item in body.get("inner", []):
        if item.get("kind") != "DeclStmt":
            break
        _, begin, end = spans[id(item)]
        if not begin or not end:
            break
        parts = item.get("inner", [])
        movable = not any(p.get("storageClass") in ("static", "extern") for p in parts) and not _calls(parts)
        out.append(Local(begin[1], end[1] + end[2], movable))
    return tuple(out)


def declarations(tree: dict, path: str | None = None) -> list[Declaration]:
    spans = _locate(tree)
    out = []

    def top(node):
        for child in node.get("inner", []):
            kind = child.get("kind")
            if kind in _TOP:
                top(child)
                continue
            at, begin, end = spans.get(id(child), (None, None, None))
            if not (at and begin and end) or not at[0] or not (at[0] == begin[0] == end[0]):
                continue
            file = at[0] if at[0] == "<stdin>" else relative((ROOT / at[0]).resolve())
            if path is not None and file != path:
                continue
            body = (
                next((b for b in child.get("inner", []) if b.get("kind") == "CompoundStmt"), None)
                if kind in _FUNCTIONS
                else None
            )
            out.append(
                Declaration(
                    kind,
                    child.get("name", ""),
                    child.get("mangledName", ""),
                    file,
                    begin[1],
                    end[1] + end[2],
                    at[1],
                    child.get("storageClass", ""),
                    bool(child.get("inline")),
                    spans[id(body)][1][1] if body else None,
                    _locals(body, spans) if body else (),
                )
            )

    top(tree)
    starts = collections.Counter((d.file, d.start) for d in out)
    return [replace(d, shared=True) if starts[(d.file, d.start)] > 1 else d for d in out]


def _skip_literal(text: str, i: int) -> int:
    quote, j, n = text[i], i + 1, len(text)
    while j < n and text[j] != quote and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
    return j + 1


def scan(text: str) -> tuple[list[tuple[int, int]], list[Directive], int]:
    comments, directives, first_code = [], [], len(text)
    i, n, line_start = 0, len(text), True
    while i < n:
        c = text[i]
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            comments.append((i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            comments.append((i, j))
            i = j
        elif c == "\n":
            line_start = True
            i += 1
        elif c in " \t\r\f\v":
            i += 1
        elif c == "#" and line_start:
            start = text.rfind("\n", 0, i) + 1
            j = i
            while j < n and text[j] != "\n":
                if text.startswith("/*", j):
                    k = text.find("*/", j + 2)
                    j = n if k < 0 else k + 2
                elif text[j] == "\\" and text.startswith("\n", j + 1):
                    j += 2
                elif text[j] in "\"'":
                    j = _skip_literal(text, j)
                else:
                    j += 1
            j = min(j, n)
            line = re.sub(r"\\\n", " ", text[i + 1 : j]).strip()
            kind = re.match(r"\w*", line)[0]
            directives.append(Directive(start, j, kind, "#" + line))
            i = j
        else:
            if c in "\"'":
                i = _skip_literal(text, i)
            else:
                i += 1
            first_code = min(first_code, i - 1)
            line_start = False
    return comments, directives, first_code


def _pragma_step(state: dict, saved: list, text: str) -> None:
    rest = text[len("#pragma") :].strip()
    rest = re.sub(r"/\*.*?\*/|//.*", "", rest).strip()
    m = re.match(r"(\w+)\s*(.*)", rest)
    if not m:
        return
    name, argument = m[1], re.sub(r"\s+", " ", m[2]).strip()
    if name in _STATELESS_PRAGMAS or (not argument and name not in ("push", "pop")):
        return
    if name == "push" and not argument:
        saved.append({k: list(v) for k, v in state.items()})
    elif name == "pop" and not argument:
        if saved:
            state.clear()
            state.update(saved.pop())
    elif name == "pack":
        inner = argument.strip("() ").replace(" ", "")
        stack = state.setdefault("pack", [])
        if inner.startswith("push"):
            stack.append(inner.split(",", 1)[1] if "," in inner else (stack[-1] if stack else ""))
        elif inner.startswith("pop"):
            if stack:
                stack.pop()
        elif stack:
            stack[-1] = inner
        else:
            stack.append(inner)
    else:
        key = name
        if name == "options" and "=" in argument:
            key, argument = f"options {argument.split('=')[0].strip()}", argument.split("=", 1)[1].strip()
        stack = state.setdefault(key, [])
        if argument == "reset":
            if stack:
                stack.pop()
        else:
            stack.append(argument)


def _frozen(state: dict) -> tuple:
    return tuple(sorted((k, tuple(v)) for k, v in state.items() if v))


def describe(state: tuple) -> str:
    return ", ".join(f"{name} {' '.join(values)}" for name, values in state) or "none"


class Text:
    def __init__(self, path: str, text: str | None = None):
        self.path = relative((ROOT / path).resolve()) if text is None else path
        self.text = (ROOT / self.path).read_bytes().decode("latin-1") if text is None else text
        self.comments, self.directives, self.first_code = scan(self.text)

    def line(self, offset: int) -> int:
        return self.text.count("\n", 0, offset) + 1

    def includes(self) -> list[Include]:
        out = []
        for x in self.directives:
            m = re.match(r'#\s*include\s*([<"][^>"]*[>"])', x.text) if x.kind == "include" else None
            if m:
                out.append(Include(x, m[1], bool(self.conditional_at(x.start)), x.start > self.first_code))
        return out

    def statement_end(self, offset: int) -> int | None:
        m = re.compile(r"\s*;").match(self.text, offset)
        return self.line_end(m.end()) if m else None

    def pragma_state(self, offset: int) -> tuple:
        state, saved = {}, []
        for directive in self.directives:
            if directive.start >= offset:
                break
            if directive.kind == "pragma":
                _pragma_step(state, saved, directive.text)
        return _frozen(state)

    def conditional_at(self, offset: int) -> tuple[int, ...]:
        stack = []
        for directive in self.directives:
            if directive.start >= offset:
                break
            if directive.kind in ("if", "ifdef", "ifndef"):
                stack.append(directive.start)
            elif directive.kind in ("elif", "else") and stack:
                stack[-1] = directive.start
            elif directive.kind == "endif" and stack:
                stack.pop()
        return tuple(stack)

    def balanced(self, start: int, end: int) -> bool:
        state, saved, depth = {}, [], 0
        for directive in self.directives:
            if start <= directive.start < end:
                if directive.kind == "pragma":
                    _pragma_step(state, saved, directive.text)
                elif directive.kind in ("if", "ifdef", "ifndef"):
                    depth += 1
                elif directive.kind in ("elif", "else") and depth == 0:
                    return False
                elif directive.kind == "endif":
                    depth -= 1
                    if depth < 0:
                        return False
        return depth == 0 and not _frozen(state) and not saved

    def line_start(self, offset: int) -> int:
        return self.text.rfind("\n", 0, offset) + 1

    def only_space(self, start: int, end: int) -> bool:
        return not self.text[start:end].strip()

    def _group(self, directive: Directive) -> list[Directive]:
        index = self.directives.index(directive)
        depth, first = 0, index
        for k in range(index, -1, -1):
            kind = self.directives[k].kind
            if kind == "endif" and k != index:
                depth += 1
            elif kind in ("if", "ifdef", "ifndef"):
                if depth == 0:
                    first = k
                    break
                depth -= 1
        group, depth = [], 0
        for k in range(first, len(self.directives)):
            kind = self.directives[k].kind
            if kind in ("if", "ifdef", "ifndef"):
                depth += 1
            if depth == 1:
                group.append(self.directives[k])
            if kind == "endif":
                depth -= 1
                if depth == 0:
                    break
        return group

    def line_end(self, offset: int) -> int:
        i = offset
        while True:
            newline = self.text.find("\n", i)
            newline = len(self.text) if newline < 0 else newline
            comment = next((c for c in self.comments if i <= c[0] < newline), None)
            if comment is None:
                return min(newline + 1, len(self.text)) if self.only_space(i, newline) else offset
            if not self.only_space(i, comment[0]):
                return offset
            i = comment[1]

    def extent(self, d: Declaration) -> tuple[int, int]:
        start, end = d.start, d.end
        line = self.line_start(start)
        if self.only_space(line, start):
            start = line
        end = self.line_end(end)
        while start > 0 and self.text[start - 1] == "\n" and start == self.line_start(start):
            above = self.line_start(start - 1)
            comment = next(
                (
                    c
                    for c in self.comments
                    if c[1] <= start
                    and self.only_space(c[1], start)
                    and self.text.count("\n", c[1], start) == 1
                    and self.only_space(self.line_start(c[0]), c[0])
                ),
                None,
            )
            if comment:
                start = self.line_start(comment[0])
                continue
            directive = next((x for x in self.directives if x.start == above and x.end == start - 1), None)
            following = next((x for x in self.directives if x.start >= end), None)
            if directive is None or end == d.end:
                break
            if directive.kind == "pragma":
                name = re.match(r"#pragma\s+(\w+)\s+(on|off)\b", directive.text)
                if (
                    name
                    and following
                    and self.only_space(end, following.start)
                    and re.match(rf"#pragma\s+{name[1]}\s+reset\b", following.text)
                ):
                    start, end = directive.start, self.line_end(following.end)
                    continue
            elif directive.kind in ("if", "ifdef", "ifndef", "elif", "else"):
                group = self._group(directive)
                position = group.index(directive)
                if position + 1 < len(group) and self.only_space(end, group[position + 1].start):
                    start, end = group[0].start, self.line_end(group[-1].end)
                    continue
            break
        return start, end


class Source(Text):
    def __init__(self, path: str, text: str | None = None, version: Version | str | None = None):
        super().__init__(path, text)
        self.version = get_version(version)
        self.compilation = build.compilation(self.version, self.path)
        tree = clang.syntax_tree(self.compilation, None if text is None else self.text)
        self.declarations = declarations(tree, self.path if text is None else "<stdin>")
        self.definitions = [d for d in self.declarations if d.defined]
        for d in self.definitions:
            if self.text[d.end - 1] != "}" or self.text[d.body] != "{":
                raise Error(f"{self.path}: clang's range of {d.name} is not its text (line {self.line(d.at)})")

    def placed(self, addresses: dict[str, int]) -> list[tuple[int, Declaration]]:
        out = []
        for d in self.definitions:
            address = addresses.get(d.name, addresses.get(d.mangled))
            if address is not None:
                out.append((address, d))
        return out

    def ordered(self, functions: list[tuple[int, Declaration]], descending: bool = False) -> tuple[str, int]:
        slots = [self.extent(d) for _, d in functions]
        wanted = sorted(range(len(functions)), key=lambda i: functions[i][0], reverse=descending)
        problems = []
        for slot, i in enumerate(wanted):
            if slot == i:
                continue
            name = functions[i][1].name
            here, there = slots[i][0], slots[slot][0]
            if not self.balanced(*slots[i]):
                problems.append(f"{name}: its text changes #pragma settings or #if nesting")
            if self.conditional_at(here) != self.conditional_at(there):
                problems.append(f"{name}: would change #if branch (line {self.line(here)} to {self.line(there)})")
            if self.pragma_state(here) != self.pragma_state(there):
                problems.append(
                    f"{name}: #pragma settings would change from {describe(self.pragma_state(here))} "
                    f"to {describe(self.pragma_state(there))}"
                )
        if problems:
            raise Error(f"{self.path}: not reordered:\n  " + "\n  ".join(problems))
        pieces, last = [], 0
        for slot, i in enumerate(wanted):
            pieces += [self.text[last : slots[slot][0]], self.text[slots[i][0] : slots[i][1]]]
            last = slots[slot][1]
        return "".join(pieces) + self.text[last:], sum(slot != i for slot, i in enumerate(wanted))

    def spelled(self, d: Declaration) -> str:
        return re.match(r"~?\w*", self.text[d.at :])[0]

    def find(self, name: str) -> Declaration:
        found = [d for d in self.definitions if name in (d.name, d.mangled)]
        if not found:
            found = [d for d in self.definitions if self.spelled(d) == name]
        if len(found) != 1:
            where = ", ".join(f"line {self.line(d.at)}" for d in found)
            raise Error(
                f"{self.path}: {'no' if not found else 'more than one'} definition of {name}"
                + (f" ({where})" if where else "")
            )
        return found[0]


def _declarations_of(job) -> list[Declaration]:
    version, source = job
    return declarations(clang.syntax_tree(build.compilation(version, source)))


def project_declarations(version: Version | str | None = None) -> list[Declaration]:
    import concurrent.futures

    from . import config

    v = get_version(version)
    jobs = [(v, s) for s in config.sources()]
    seen, out = set(), []
    with concurrent.futures.ProcessPoolExecutor(int(os.environ.get("JOBS") or 0) or None) as pool:
        for found in pool.map(_declarations_of, jobs, chunksize=4):
            for d in found:
                key = (d.file, d.start, d.at, d.name)
                if key not in seen:
                    seen.add(key)
                    out.append(d)
    return out
