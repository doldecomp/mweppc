"""A tolerant index of the function definitions in a C or C++ tree (a reference decompilation such as Ninji's Pro7, the
gc1 decompilation, MSL, this repository's src/), for the naming evidence: each definition's name, file, line, whether
static, the string literals and callees in its body, the lines its asserts pass, and its body's tokens (for comparing
two trees' bodies).

Not a parser, and not for editing sources: a definition is a top-level `name(...) {` at brace depth 0, with comments
and preprocessor lines blanked. Trees that do not compile with this repository's headers are its point.
"""

from __future__ import annotations

import re
from pathlib import Path

from .project import Error, relative

KEYWORDS = {"if", "while", "for", "switch", "return", "sizeof", "do", "else", "case", "defined"}
ASSERT_MACROS = ("CError_ASSERT", "CError_FATAL", "CError_FAIL", "IRO_ASSERT", "OS_ASSERT", "COS_ASSERT", "PA_ASSERT")
_ASSERT = re.compile(r"\b(?:" + "|".join(ASSERT_MACROS) + r")\s*\(\s*(\d+)")
_CALL = re.compile(r"\b([A-Za-z_]\w*)\s*\(")
_TOKEN = re.compile(r'"(?:[^"\\\n]|\\.)*"|[A-Za-z_]\w*|\d\w*|->|\+\+|--|[^\s\w]')
_STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
SUFFIXES = (".c", ".cpp", ".cp", ".h")


def _unescape(s: str) -> str:
    try:
        return bytes(s, "latin-1").decode("unicode_escape")
    except UnicodeDecodeError:
        return s


def _skip_literal(text: str, i: int) -> int:
    """The index after the string or character literal starting at I (or at the end of its line)."""
    quote, j, n = text[i], i + 1, len(text)
    while j < n and text[j] != quote and text[j] != "\n":
        j += 2 if text[j] == "\\" else 1
    return j + 1


def _strip_comments(text: str) -> str:
    """TEXT with its comments removed (their lines kept); literals kept."""
    out, i, n = [], 0, len(text)
    while i < n:
        if text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        elif text.startswith("//", i):
            j = text.find("\n", i)
            i = n if j < 0 else j
        elif text[i] in "\"'":
            j = _skip_literal(text, i)
            out.append(text[i:j])
            i = j
        else:
            out.append(text[i])
            i += 1
    return "".join(out)


def _definition_name(head: str) -> str | None:
    """The name a top-level `HEAD {` defines, if it is a function definition."""
    h = head.rstrip()
    if (
        not h.endswith(")")
        or re.search(r"\b(struct|union|enum|class|namespace)\b[^;()]*$", h)
        or "=" in h.split("(")[0]
    ):
        return None
    depth, k = 0, len(h) - 1
    while k >= 0:
        if h[k] == ")":
            depth += 1
        elif h[k] == "(":
            depth -= 1
            if depth == 0:
                break
        k -= 1
    m = re.search(r"((?:[A-Za-z_]\w*::)*~?[A-Za-z_]\w*)\s*$", h[:k]) or re.search(
        r"\b[A-Z_][A-Z0-9_]*\s*\(\s*([A-Za-z_]\w*)\s*\)\s*$", h[:k]
    )
    if m and m[1].split("::")[-1] not in KEYWORDS:
        return m[1]
    return None


def functions(path: Path) -> list[dict]:
    """The function definitions in the file at PATH."""
    text = _strip_comments(Path(path).read_text(encoding="latin-1"))
    text = re.sub(r"^[ \t]*#.*?(?<!\\)$", lambda m: " " * len(m[0]), text, flags=re.MULTILINE)
    out = []
    i, n = 0, len(text)
    last_top = 0  # (after the last top-level ; or })
    while i < n:
        c = text[i]
        if c in "\"'":
            i = _skip_literal(text, i)
            continue
        if c == "{":
            if re.search(r'(\bextern\s+"C"|\bnamespace(\s+\w+)?)\s*$', text[last_top:i]):
                # (a linkage block or a namespace: its definitions are top level)
                i += 1
                last_top = i
                continue
            head = text[last_top:i]
            name = _definition_name(head)
            depth, j = 0, i
            while j < n:
                ch = text[j]
                if ch in "\"'":
                    j = _skip_literal(text, j)
                    continue
                if ch == "{":
                    depth += 1
                elif ch == "}":
                    depth -= 1
                    if depth == 0:
                        break
                j += 1
            body = text[i : j + 1]
            if name:
                out.append(
                    dict(
                        name=name,
                        line=text.count("\n", 0, i) + 1,
                        static=bool(re.search(r"\bstatic\b", head)),
                        strings=[_unescape(s) for s in _STRING.findall(body)],
                        calls=[x for x in _CALL.findall(body) if x not in KEYWORDS],
                        asserts=sorted({int(x) for x in _ASSERT.findall(body)}),
                        lines=body.count("\n") + 1,
                        tokens=_TOKEN.findall(body),
                    )
                )
            i = j + 1
            last_top = i
            continue
        if c in ";}":
            last_top = i + 1
        i += 1
    return out


def index(root: Path | str, suffixes=SUFFIXES) -> list[dict]:
    """Every definition under ROOT, each with its "file" (relative to ROOT). A missing tree is an error: an empty
    index would silently weaken the evidence."""
    root = Path(root)
    if not root.is_dir():
        raise Error(f"{relative(root)} does not exist")
    out = []
    for path in sorted(root.rglob("*")):
        if path.suffix not in suffixes or ".git" in path.parts or not path.is_file():
            continue
        for f in functions(path):
            f["file"] = path.relative_to(root).as_posix()
            out.append(f)
    return out
