#!/usr/bin/env python3
"""python tools/evidence.py COMMAND ...: what independent evidence says each of the original's functions is, and the
decisions made from it. The evidence: the original's own assert sites (files and lines), a reference decompilation
of a nearby compiler (Ninji's Mac Pro 7 tree; its assert lines often equal 2.6's), the Wii compilers (later builds of
the same sources, whose CodeView names every function's object and the public ones), MSL in lib/, and the gc1
decompilation (whose names are partly invented).

  pair [--version V] [--exe EXE]
      Pair the version's functions with a Wii compiler's by their code, no names used (tools/mw/pairing.py; default
      EXE build/compilers/Wii/1.0RC1/mwcceppc.exe). Writes build/V/wii_match.json: each row's Wii function (address,
      size, module, public name) with the pairing's method and similarity. Run again when the rows' bounds change.
  sweep [--version V] [--pro7 DIR] [--gc1 DIR] [-o OUT]
      Every row's name and unit checked against the evidence (after a build and `pair`): writes build/V/name_sweep.json
      and the list (default docs/names-V.md), with the review's decisions over the evidence's verdicts. Evidence kinds:
        provenance  where the row's name exists: Pro7 (P), a Wii compiler's publics (W), MSL and the runtime in lib/
                    (M), gc1 (g); a name only gc1 has, or nobody, was made up: "unconfirmed", not wrong
        assert      the Pro7 function in the file the row's own asserts name whose assert lines are the same (exactly,
                    or at one offset: 2.6's lines are Pro7's shifted after its additions); "assert line" a single line
                    at the offset of the sure matches around it
        strings     the Pro7 function whose literals include strings the row references, when few Pro7 functions do
        wii         the paired Wii function's name and module: strong when anchored by strings/imports/constants or
                    similarity >= 0.65, medium >= 0.5, weak below
        order       each unit's rows aligned in order (or reversed: deferred units) with the Pro7 files they point to
        body        the repository's body for the row is Pro7's function of that name (and exact)
      Verdicts: confirmed, likely, plausible, unconfirmed, unnamed (fn_), check, rename, conflict; reviewed and open
      (a decision of the review; a note starting "OPEN" keeps a row undecided).
  review [--version V] {show,accept,set,ok,unit,drop,batch} ...
      Record decisions in config/V/name_review.json (run sweep after):
        show SOURCE...                      the rows of the sources (substrings) with the sweep's verdicts and evidence
        accept ADDRESS... [-n NOTE]         the sweep's proposed name (or the row's own, when it proposes none)
        set ADDRESS NAME [-n NOTE] [-u UNIT]  the row's name is NAME (and its unit UNIT, when not its source)
        ok ADDRESS... [-n NOTE]             the row's current name stands
        unit SOURCE NOTE...                 a finding about a unit's bounds
        drop ADDRESS...                     forget the decisions
        batch FILE [-n NOTE] [-u UNIT]      a `set` per line: ADDRESS NAME [| NOTE] (NAME `=` keeps the row's name;
                                            # comments)
      ADDRESS is a row's address (0x00436e10 or 436e10); A-B takes every row from A to B. Each decision records the
      row's name when it was made ("was").
  apply [--version V] [--write] [--only NAME...] [--force NAME...]
      Rename the rows as the decisions say, everywhere (tools/mw/renaming.py: every version, all at once). A decision
      applies while its row still has the name it was made over; one whose row was renamed since is stale and listed.
      A rename whose new name already means something in the code is held back and listed with its uses (--force
      NAME, old or new, applies it once the code is ready); so is one onto a name a held rename keeps. Names with `::`
      (C++ members) are left. Without --write, prints what it would do.
  unit [--version V] UNIT | START END
      The original's functions over one unit's .text (a substring of its name) or an address range, each with its
      state, name, whether a Wii compiler has a public of that name (and its module), its source when another unit's,
      and the files and lines its own asserts push.
  units [--version V] [--pro7 DIR] [-o OUT]
      The table of translation units (default docs/units-V-table.md): each unit's range, exact functions and bytes,
      host compiler, order ("rev": its asserts' lines descend along .text, so it was built -inline auto,deferred;
      "asc", "mixed", or "-" with fewer than three), the evidence for its file name (its own asserts, a Wii module,
      Pro7's tree), and whether it links.
  align UNIT [--version V] [--rev] [--pro7 DIR]
      A unit's functions in the original beside the definitions of the Pro7 file of that name (statics marked "s"),
      to name them and find the ones it inlined or the linker dropped. --rev lists the original's in descending
      address order (a unit built -inline auto,deferred emits its functions in reverse source order).

The reference trees default to ../MWCC_Pro7 and ../mwcc-gc1/src; a missing tree is an error, not empty evidence.
"""

import argparse
import bisect
import collections
import difflib
import functools
import itertools
import json
import math
import re
import signal
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import asserts, build, cindex, config, pairing, project, renaming, verdict, wii
from mw.pe import original

PRO7 = project.ROOT.parent / "MWCC_Pro7"
GC1 = project.ROOT.parent / "mwcc-gc1/src"
WII_COMPILERS = project.ROOT / "build/compilers/Wii"

STRENGTHS = ["weak", "medium", "strong"]

# (units the trees name differently: Pro7's Mac-hosted OSLib is Posix.c where the Windows host's is MsDos.c; the
# repository's MSL units carry a "_g" suffix the library's files lack; 2.x spells "eabi-ppc" where Pro7 spells
# "mach-ppc" and Wii "eppc")
UNIT_ALIASES = {
    "posix": "msdos",
    "printf": "printf_g",
    "time": "time_g",
    "alloc": "alloc_g",
    "buffer_io": "buffer_io_g",
    "direct_io": "direct_io_g",
    "fcntl.win32": "fcntl_win32_g",
    "file_io.win32": "file_io_win32_g",
    "file_pos": "file_pos_g",
    "pool_alloc.win32": "pool_alloc_win32_g",
    "qsort": "qsort_g",
    "startup.win32": "startup_win32",
    "threadlocaldata": "startup_win32",
    "sysenv.win32": "sysenv_win32",
    "time.win32": "time_win32",
    "unistd.win32": "unistd_win32",
    "abort_exit_win32": "abort_exit",
    "critical_regions.win32": "critical_regions_win32",
    "setjmp.x86": "setjmp_x86",
    "iropointeranalysisadts": "iropointeranalysis",
}


def demangle(name):
    """A plain name for a C++ one: `?f@@YA...` -> f, `?f@C@@...` -> C::f, `??0C@@` -> C::C."""
    if not name.startswith("?"):
        return name
    m = re.match(r"\?\?(\d|_\w)(\w+)@", name)
    if m:
        return f"{m[2]}::{'~' if m[1] == '1' else ''}{m[2]}" if m[1] in "01" else name
    parts = name[1:].split("@@", 1)[0].split("@")
    return "::".join(reversed(parts))


def short(name):
    return demangle(name).split("::")[-1]


@functools.cache
def stem(path):
    return Path(path).name.rsplit(".", 1)[0].lower() if path else None


def unit_key(path):
    """A unit's name for comparing trees."""
    s = stem(path) or ""
    for target in ("mach-ppc", "eabi-ppc", "ppc-mach", "ppc-eabi", "eppc"):
        s = s.replace(target, "TARGET")
    return UNIT_ALIASES.get(s, s)


def version_doc_name(version):
    return str(version).removeprefix("GC_").replace("_", ".")


def wii_executables():
    found = sorted(WII_COMPILERS.glob("*/mwcceppc.exe"))
    if not found:
        raise project.Error(f"{project.relative(WII_COMPILERS)} has no compilers: ninja downloads them")
    return found


def wii_names():
    """{short name: module} of every Wii compiler's functions (the first compiler naming it)."""
    out = {}
    for exe in wii_executables():
        image = wii.read(exe)
        for address, name in image.names().items():
            out.setdefault(short(name), image.module_at(address))
    return out


# ----------------------------------------------------------------------------------------------------------- pair


def rows_digest(rows):
    return project.digest(json.dumps([[r.address, r.size] for r in rows]).encode())


def pair(version, exe):
    rows = config.functions(version)
    image = wii.read(exe)
    names = image.names()
    ours = [pairing.Function(r.address, r.size) for r in rows]
    theirs = [pairing.Function(c.address, c.size, c.module) for c in image.functions()]
    pairing.features(original(version).path, ours, asserts.targets(rows))
    pairing.features(image.pe.path, theirs, {a for a, n in names.items() if n in asserts.FUNCTIONS})
    matcher = pairing.Matcher(ours, theirs)
    matcher.run(lambda text: print(text, flush=True))
    out = []
    for i, row in enumerate(rows):
        entry = dict(address=config.format_address(row.address), name=row.name, source=row.source)
        if i in matcher.g2w:
            w = theirs[matcher.g2w[i]]
            how, score = matcher.how[i]
            entry.update(
                wii_address=config.format_address(w.address),
                wii_size=w.size,
                module=w.module,
                wii_name=names.get(w.address),
                method=how,
                similarity=score,
                instructions=min(len(ours[i].sequence), len(w.sequence)),
            )
        out.append(entry)
    path = version.build / "wii_match.json"
    project.write_atomic(
        path, json.dumps(dict(rows=rows_digest(rows), exe=project.relative(exe), functions=out), indent=1) + "\n"
    )
    methods = collections.Counter(matcher.how[i][0] for i in matcher.g2w)
    print(
        f"{len(matcher.g2w)} of {len(rows)} functions paired with {Path(exe).parent.name}'s: "
        + ", ".join(f"{m} {n}" for m, n in methods.most_common())
        + f" ({project.relative(path)})"
    )


def load_pairs(version, rows):
    path = version.build / "wii_match.json"
    if not path.exists():
        raise project.Error(
            f"{project.relative(path)} does not exist: python tools/evidence.py pair --version {version}"
        )
    data = json.loads(path.read_text())
    if data.get("rows") != rows_digest(rows):
        raise project.Error(
            f"{project.relative(path)} was made for other rows (functions.json's bounds changed): "
            f"python tools/evidence.py pair --version {version}"
        )
    return {e["address"]: e for e in data["functions"]}


# ---------------------------------------------------------------------------------------------------------- sweep


def similar_tokens(a, b, floor=0.0):
    """The token sequences' similarity (difflib's ratio), or 0.0 when it is surely no more than FLOOR."""
    if not a or not b or not 0.33 <= len(a) / len(b) <= 3:
        return 0.0
    matcher = difflib.SequenceMatcher(None, a, b, autojunk=False)
    # (the quick ratios bound the ratio from above, and cost little next to it)
    if floor and (matcher.real_quick_ratio() <= floor or matcher.quick_ratio() <= floor):
        return 0.0
    return matcher.ratio()


def assert_match(lines, candidates):
    """[(score, exact, function, offset)] of CANDIDATES (Pro7 functions) by their assert lines against LINES: the
    Jaccard overlap at the best single offset; 1.0 at offset 0 is "exact"."""
    out = []
    lines = set(lines)
    for f in candidates:
        theirs = set(f["asserts"])
        if not theirs:
            continue
        best, exact, offset = 0.0, False, None
        for d in {t - l for t in theirs for l in lines}:
            shifted = {l + d for l in lines}
            score = len(shifted & theirs) / len(shifted | theirs)
            if score > best or score == best and d == 0:
                best, exact, offset = score, d == 0 and score == 1.0, d
        out.append((best, exact, f, offset))
    out.sort(key=lambda x: (-x[0], not x[1]))
    return out


def align(rows, functions, score):
    """Monotone pairing of ROWS with FUNCTIONS (each in its order) maximizing the sum of SCORE(row, function) > 0."""
    n, m = len(rows), len(functions)
    table = [[0.0] * (m + 1) for _ in range(n + 1)]
    cache = {}
    for a in range(1, n + 1):
        for b in range(1, m + 1):
            s = cache[a, b] = score(rows[a - 1], functions[b - 1])
            table[a][b] = max(table[a - 1][b], table[a][b - 1], table[a - 1][b - 1] + s if s > 0 else -1)
    pairs, a, b = [], n, m
    while a and b:
        s = cache[a, b]
        if s > 0 and table[a][b] == table[a - 1][b - 1] + s:
            pairs.append((a - 1, b - 1, s))
            a, b = a - 1, b - 1
        elif table[a][b] == table[a - 1][b]:
            a -= 1
        else:
            b -= 1
    return table[n][m], pairs[::-1]


def align_costed(rows, functions, score, skip_row, skip_function):
    """Monotone pairing of ROWS with FUNCTIONS maximizing the pairs' SCORE less SKIP_ROW(row) for each row left out
    and SKIP_FUNCTION(function) for each function left out (pairs scoring None are not allowed)."""
    n, m = len(rows), len(functions)
    neg = float("-inf")
    table = [[neg] * (m + 1) for _ in range(n + 1)]
    table[0][0] = 0.0
    for a in range(n + 1):
        for b in range(m + 1):
            if a == b == 0:
                continue
            best = neg
            if a:
                best = max(best, table[a - 1][b] - skip_row(rows[a - 1]))
            if b:
                best = max(best, table[a][b - 1] - skip_function(functions[b - 1]))
            if a and b:
                s = score(rows[a - 1], functions[b - 1])
                if s is not None:
                    best = max(best, table[a - 1][b - 1] + s)
            table[a][b] = best
    pairs, a, b = [], n, m
    while a or b:
        if a and b:
            s = score(rows[a - 1], functions[b - 1])
            if s is not None and table[a][b] == table[a - 1][b - 1] + s:
                pairs.append((a - 1, b - 1, s))
                a, b = a - 1, b - 1
                continue
        if a and table[a][b] == table[a - 1][b] - skip_row(rows[a - 1]):
            a -= 1
        else:
            b -= 1
    return pairs[::-1]


def read_review(version):
    path = version.config / "name_review.json"
    return json.loads(path.read_text()) if path.exists() else {"functions": {}, "units": {}}


def sweep(version, pro7_root, gc1_root, output=None):
    pe = original(version)
    exact = {a for a, v in verdict.load(version).items() if v.exact}
    rows = config.functions(version)
    address_of = {r.address: i for i, r in enumerate(rows)}
    targets = asserts.targets(rows)

    print("indexing sources", file=sys.stderr)
    pro7 = cindex.index(pro7_root)
    gc1 = cindex.index(gc1_root)
    msl = cindex.index(project.ROOT / "lib")
    ours = cindex.index(project.ROOT / "src")
    pro7_by_name = collections.defaultdict(list)
    for f in pro7:
        pro7_by_name[short(f["name"])].append(f)
    pro7_by_stem = collections.defaultdict(list)
    for f in pro7:
        pro7_by_stem[stem(f["file"])].append(f)
    for fs in pro7_by_stem.values():
        fs.sort(key=lambda f: f["line"])
    gc1_names = {short(f["name"]) for f in gc1}
    msl_names = {short(f["name"]) for f in msl}
    ours_by_name = collections.defaultdict(list)
    for f in ours:
        ours_by_name[short(f["name"])].append(f)
    string_owners = collections.defaultdict(set)  # (a string literal's Pro7 owners)
    for k, f in enumerate(pro7):
        for s in f["strings"]:
            if len(s) >= 3:
                string_owners[s].add(k)
    names_wii = wii_names()
    match = load_pairs(version, rows)

    print("reading the original's functions", file=sys.stderr)
    info = []
    for r in rows:
        instructions, found = asserts.sites(pe, pe.read(r.address, r.size), r.address, targets)
        strings, callees = [], []
        for ins in instructions:
            for value in re.findall(r"0x[0-9a-f]+", ins.op_str):
                v = int(value, 16)
                if ins.mnemonic == "call" and v in address_of:
                    callees.append(address_of[v])
                elif v > pe.image_base:
                    try:
                        text = pe.read(v, 128).split(b"\0", 1)[0]
                    except ValueError:
                        continue
                    if len(text) >= 3 and re.fullmatch(rb"[\x09\x0a\x0d\x20-\x7e]+", text):
                        strings.append(text.decode("latin-1"))
        files = {s.file for s in found}
        info.append(dict(asserts=found, strings=[s for s in strings if s not in files], callees=callees))

    entries = []
    line_samples = collections.defaultdict(list)  # (file stem: [(2.6 line, offset to Pro7's)] of sure matches)
    for i, r in enumerate(rows):
        plain = short(r.name)
        e = dict(
            address=config.format_address(r.address),
            size=r.size,
            name=r.name,
            source=r.source,
            exact=r.address in exact,
            evidence=[],
        )
        # (a library function's name may carry its unit as a prefix, for the global row names: alloc_g_link)
        library = (r.source or "").startswith(("src/msl/", "src/runtime/"))
        unprefixed = plain.removeprefix(Path(r.source or "").stem + "_") if library else plain
        e["provenance"] = "".join(
            c
            for c, ok in (
                ("P", plain in pro7_by_name),
                ("W", plain in names_wii),
                ("M", unprefixed in msl_names),
                ("g", plain in gc1_names),
            )
            if ok
        )
        if library and e["exact"]:
            e["evidence"].append(dict(kind="library", name=plain, strength="strong", detail="exact from lib/'s source"))
        if plain in pro7_by_name:
            e["pro7_files"] = sorted({f["file"] for f in pro7_by_name[plain]})
        if plain in names_wii:
            e["name_module"] = names_wii[plain]
        sites = info[i]["asserts"]
        if sites:
            e["assert_files"] = sorted({s.file for s in sites})
            files = [f for f in e["assert_files"] if not f.endswith(".h")]
            if files:
                lines = [s.line for s in sites if s.file == files[0]]
                candidates = pro7_by_stem.get(stem(files[0]), [])
                ranked = assert_match(lines, candidates)
                # (the same lines exactly, in one Pro7 function of the file: that one, however few the lines; else
                # the one sharing most lines unshifted, when half of them; two lines fit many functions at some shift)
                exact_ones = [x for x in ranked if x[1]]
                unshifted = sorted(
                    (
                        (len(set(lines) & set(f["asserts"])) / len(set(lines) | set(f["asserts"])), f)
                        for f in candidates
                        if f["asserts"]
                    ),
                    key=lambda x: -x[0],
                )
                if len(exact_ones) == 1:
                    ranked = exact_ones
                elif (
                    unshifted and unshifted[0][0] >= 0.5 and (len(unshifted) == 1 or unshifted[0][0] > unshifted[1][0])
                ):
                    ranked = [(unshifted[0][0], False, unshifted[0][1], 0)]
                if ranked and ranked[0][0] >= 0.5 and (len(ranked) == 1 or ranked[0][0] > ranked[1][0]):
                    score, is_exact, f, offset = ranked[0]
                    if len(set(lines)) >= 2 and score >= 0.8 or is_exact:
                        line_samples[stem(files[0])] += [(l, offset) for l in lines]
                    e["evidence"].append(
                        dict(
                            kind="assert",
                            name=short(f["name"]),
                            file=f["file"],
                            strength="strong"
                            if is_exact or score >= 0.8 and (offset == 0 or len(set(lines)) >= 3)
                            else "medium",
                            detail=f"{'exact' if is_exact else f'{score:.2f}'} lines",
                        )
                    )
        votes = collections.Counter()  # (strings few Pro7 functions have)
        for s in sorted(set(info[i]["strings"])):
            owners = string_owners.get(s, ())
            if 1 <= len(owners) <= 2:
                for k in sorted(owners):
                    votes[k] += 1 / len(owners)
        if votes:
            (k, v), *rest = votes.most_common()
            if not rest or v > rest[0][1]:
                e["evidence"].append(
                    dict(
                        kind="strings",
                        name=short(pro7[k]["name"]),
                        file=pro7[k]["file"],
                        strength="strong" if v >= 2 else "medium",
                        detail=f"{v:g} string(s)",
                    )
                )
        w = match.get(e["address"])
        if w and w.get("module"):
            sim, method = w["similarity"], w["method"]
            anchored = method.split("-")[0] in ("strings", "imports", "constants")
            strength = "strong" if sim >= 0.65 or anchored and sim >= 0.45 else "medium" if sim >= 0.5 else "weak"
            # (a short function matches many: by its calls alone it says little, by its place a little more)
            if w.get("instructions", 99) < 8 and not anchored:
                strength = (
                    "weak" if method in ("callee", "caller", "global") else min(strength, "medium", key=STRENGTHS.index)
                )
            e["wii"] = dict(
                module=w["module"],
                name=w.get("wii_name") and short(w["wii_name"]),
                similarity=sim,
                method=method,
                strength=strength,
            )
            if w.get("wii_name"):
                e["evidence"].append(
                    dict(
                        kind="wii",
                        name=short(w["wii_name"]),
                        module=w["module"],
                        strength=strength,
                        detail=f"{method} {sim:.2f}",
                    )
                )
        if plain in pro7_by_name and plain in ours_by_name and r.source:
            mine = [f for f in ours_by_name[plain] if stem(f["file"]) == stem(r.source)] or ours_by_name[plain]
            # (the most similar pair, the first of equals; only a ratio of at least 0.6 counts)
            best = (0.0, None)
            for m in mine:
                for p in pro7_by_name[plain]:
                    score = similar_tokens(m["tokens"], p["tokens"], max(best[0], 0.6 - 1e-9))
                    if score > best[0]:
                        best = (score, p)
            if best[0] >= 0.6:
                e["evidence"].append(
                    dict(
                        kind="body",
                        name=plain,
                        file=best[1]["file"],
                        strength="strong" if e["exact"] else "medium",
                        detail=f"{best[0]:.2f} tokens{', exact' if e['exact'] else ''}",
                    )
                )
        entries.append(e)

    assert_positions(entries, info, line_samples, pro7_by_stem, pro7_by_name)
    order_and_verdicts(entries, rows, info, pro7, pro7_by_name, pro7_by_stem)
    for e in entries:
        homes = {}
        files = [f for f in e.get("assert_files", []) if not f.endswith(".h")]
        if files:
            homes["assert"] = unit_key(files[0])
        if e.get("wii") and e["wii"]["strength"] != "weak":
            homes["wii"] = unit_key(e["wii"]["module"])
        name = e.get("proposed") if e["verdict"] in ("rename", "conflict") else short(e["name"])
        files = {ev["file"] for ev in e["evidence"] if ev["name"] == name and ev.get("file")}
        if not files and e["verdict"] in ("confirmed", "likely", "plausible", "check"):
            files = set(e.get("pro7_files", []))
        if len({stem(f) for f in files}) == 1:
            homes["pro7"] = unit_key(next(iter(files)))
        e["homes"] = homes

    review = read_review(version)
    for e in entries:
        decision = review["functions"].get(e["address"])
        if decision:
            e["auto"] = dict(verdict=e["verdict"], proposed=e.get("proposed"))
            e["reviewed"] = decision
            name = decision.get("name", short(e["name"]))
            e["verdict"] = (
                "open"
                if decision.get("note", "").startswith("OPEN")
                else "ok"
                if name == short(e["name"]) and not decision.get("unit")
                else "reviewed"
            )
            e["proposed"] = name if name != short(e["name"]) else None
    checks = consistency(entries, pro7_by_name)
    out = version.build / "name_sweep.json"
    project.write_atomic(out, json.dumps(entries, indent=1) + "\n")
    counts = collections.Counter(e["verdict"] for e in entries)
    print(", ".join(f"{k} {v}" for k, v in counts.most_common()))
    print(
        f"{len(checks['duplicates'])} names on several rows, {len(checks['out_of_order'])} rows out of Pro7's order, "
        f"{sum(1 for u in checks['units'] if u['ordered'] and u['reversed'] and not u['deferred'])} reversed units "
        f"not built deferred",
        file=sys.stderr,
    )
    path = Path(output) if output else project.ROOT / f"docs/names-{version_doc_name(version)}.md"
    write_report(entries, review, version, path, checks)
    print(project.relative(path))


def assert_positions(entries, info, line_samples, pro7_by_stem, pro7_by_name):
    """Assert position: a line of 2.6's, shifted by the offset of the sure matches around it, is a Pro7 assert's line:
    that function's. One line is enough where the offsets on both sides agree. A file with few sure matches takes the
    offsets two or more functions' lines share, kept where they rise along the file (2.6 grew from Pro7 by
    additions, so the offset grows)."""
    rows_lines = collections.defaultdict(list)
    for i, x in enumerate(info):
        for s in x["asserts"]:
            if not s.file.endswith(".h"):
                rows_lines[stem(s.file)].append((s.line, i))
    for file, items in rows_lines.items():
        if len(line_samples.get(file, ())) >= 6 or file not in pro7_by_stem:
            continue
        theirs = {a for f in pro7_by_stem[file] for a in f["asserts"]}
        support = collections.defaultdict(set)
        for l, i in items:
            for a in theirs:
                if -400 <= a - l <= 50:
                    support[a - l].add(i)
        votes = sorted(
            (l, d)
            for l, i in items
            for d, owners in support.items()
            if len(owners) >= 2 and l + d in theirs and i in owners
        )
        # (the longest chain of samples whose offsets (Pro7 line - 2.6 line) do not rise along the file)
        best = []
        for k, (l, d) in enumerate(votes):
            chain = max((c for c in best[:k] if c[-1][1] >= d), key=len, default=[])
            best.append(chain + [(l, d)])
        if best:
            line_samples[file] = sorted(set(line_samples.get(file, [])) | set(max(best, key=len)))
    for samples in line_samples.values():
        samples.sort()
    for i, e in enumerate(entries):
        if any(ev["kind"] == "assert" and ev["strength"] == "strong" for ev in e["evidence"]):
            continue
        files = [f for f in e.get("assert_files", []) if not f.endswith(".h")]
        if not files or not line_samples.get(stem(files[0])):
            continue
        samples = line_samples[stem(files[0])]
        lines = sorted({s.line for s in info[i]["asserts"] if s.file == files[0]})
        hits = collections.Counter()
        agreed = True
        for l in lines:
            below = [d for x, d in samples if x <= l][-1:]
            above = [d for x, d in samples if x >= l][:1]
            offsets = set(below + above)
            agreed &= len(offsets) == 1
            owners = {
                short(f["name"]) for d in sorted(offsets) for f in pro7_by_stem[stem(files[0])] if l + d in f["asserts"]
            }
            for o in sorted(owners):
                hits[o] += 1
        if hits:
            (name, n), *rest = hits.most_common()
            if n == len(lines) and (not rest or rest[0][1] < n):
                f = next(f for f in pro7_by_stem[stem(files[0])] if short(f["name"]) == name)
                e["evidence"].append(
                    dict(
                        kind="assertpos",
                        name=name,
                        file=f["file"],
                        strength="strong" if agreed else "medium",
                        detail=f"{n} line(s) at the neighbours' offset",
                    )
                )
    # (one row per Pro7 function: a function several rows reach by position says the offsets are wrong there)
    claims = collections.Counter(ev["name"] for e in entries for ev in e["evidence"] if ev["kind"] == "assertpos")
    for e in entries:
        e["evidence"] = [ev for ev in e["evidence"] if ev["kind"] != "assertpos" or claims[ev["name"]] == 1]

    # (inlining: a function's asserts include its inlined callees' (and theirs): an assert naming a function its Pro7
    # namesake calls says nothing against the name)
    for e in entries:
        own = pro7_by_name.get(short(e["name"]), [])
        first = {c for f in own for c in f["calls"]}
        second = {c for name in first for f in pro7_by_name.get(name, []) for c in f["calls"]}
        for ev in e["evidence"]:
            if (
                ev["kind"] in ("assert", "assertpos")
                and ev["name"] != short(e["name"])
                and ev["name"] in first | second
            ):
                ev["kind"], ev["strength"] = "inlined", "weak"
                ev["detail"] += f"; inlined in {short(e['name'])}"


def order_and_verdicts(entries, rows, info, pro7, pro7_by_name, pro7_by_stem):
    """Each source's rows aligned with the Pro7 files its rows point to, then each row's verdict; three rounds, each
    round's names for the rows (the confident proposals, else the rows' own) the next round's names of callees and
    callers."""
    # (bytes of 2.6 code per Pro7 token, over the functions whose body is Pro7's and exact)
    ratios = sorted(
        e["size"] / len(p["tokens"])
        for e in entries
        for ev in e["evidence"]
        if ev["kind"] == "body" and e["exact"]
        for p in pro7_by_name[ev["name"]]
        if p["file"] == ev["file"] and p["tokens"]
    )
    per_token = ratios[len(ratios) // 2] if ratios else 1.0

    def size_fit(i, f):
        if not f["tokens"]:
            return 0.0
        return max(0.0, 1 - abs(math.log(rows[i].size / len(f["tokens"]) / per_token)) / math.log(3))

    by_source = collections.defaultdict(list)
    for i, r in enumerate(rows):
        by_source[r.source].append(i)
    callers = collections.defaultdict(list)
    for i, x in enumerate(info):
        for c in x["callees"]:
            callers[c].append(i)
    pro7_names = set(pro7_by_name)
    pro7_callers = collections.defaultdict(set)
    for f in pro7:
        for c in f["calls"]:
            pro7_callers[c].add(short(f["name"]))
    effective = {i: short(r.name) for i, r in enumerate(rows)}

    def assert_fit(i, f):
        lines = [s.line for s in info[i]["asserts"] if stem(s.file) == stem(f["file"])]
        return assert_match(lines, [f])[0][0] if lines and f["asserts"] else 0.0

    def anchor_score(i, f):
        """What says row I is Pro7 function F, besides their places."""
        s = 0.0
        for ev in entries[i]["evidence"]:
            if ev.get("file") == f["file"] and ev["name"] == short(f["name"]):
                s += {"assert": 4, "assertpos": 3, "strings": 2, "body": 2}.get(ev["kind"], 0)
            if ev["kind"] == "wii" and ev["name"] == short(f["name"]) and ev["strength"] != "weak":
                s += 2
        if effective[i] == short(f["name"]):
            s += 2
        fit = assert_fit(i, f)
        if fit >= 0.5 and len(f["asserts"]) >= 2:
            s += 1.5 * fit
        # (the call graph by name: the callees' and the callers' names against the Pro7 function's)
        for mine, theirs in (
            ({effective[c] for c in info[i]["callees"]}, set(f["calls"])),
            ({effective[c] for c in callers[i]}, pro7_callers.get(short(f["name"]), set())),
        ):
            mine = {c for c in mine if c in pro7_by_name}
            theirs = theirs & pro7_names
            if mine and theirs and mine & theirs:
                overlap = len(mine & theirs) / len(mine | theirs)
                if overlap >= 0.3:
                    s += 2 * overlap
        return s if s >= 1.5 else 0.0

    def gap_score(i, f):
        return 0.6 * size_fit(i, f) + assert_fit(i, f)

    for sweep_round in range(3):
        for e in entries:
            e["evidence"] = [ev for ev in e["evidence"] if ev["kind"] != "order"]
            for key in ("order", "gap_candidates", "proposed", "hint", "note"):
                e.pop(key, None)
        gaps = {}
        for source, indices in by_source.items():
            if not source:
                continue
            files = collections.Counter()
            for i in indices:
                e = entries[i]
                for ev in e["evidence"]:
                    if ev.get("file"):
                        files[ev["file"]] += 2
                for f in e.get("pro7_files", []):
                    files[f] += 1 / len(e["pro7_files"])
                for a in e.get("assert_files", []):
                    for f in sorted({g["file"] for g in pro7_by_stem.get(stem(a), [])}):
                        files[f] += 1
            for f in sorted({g["file"] for g in pro7_by_stem.get(stem(source), [])}):
                files[f] += 3
            for file in [f for f, v in files.most_common(4) if v >= 3]:
                functions = [f for f in pro7_by_stem[stem(file)] if f["file"] == file]
                forward = align(indices, functions, anchor_score)
                backward = align(indices, functions[::-1], anchor_score)
                total, anchors = max(forward, backward, key=lambda x: x[0])
                if total < 3:
                    continue
                order = functions if total == forward[0] else functions[::-1]
                direction = "forward" if order is functions else "reverse"
                found = [(a, b, s, "anchor") for a, b, s in anchors]
                # (between anchors: as many rows as Pro7 functions pair in order; else by size and asserts, as hints)
                bounds = [(-1, -1)] + [(a, b) for a, b, _ in anchors] + [(len(indices), len(order))]
                for (a1, b1), (a2, b2) in zip(bounds, bounds[1:]):
                    left, right = list(range(a1 + 1, a2)), list(range(b1 + 1, b2))
                    if not left or not right:
                        continue
                    edge = a1 == -1 or a2 == len(indices)
                    if (
                        len(left) == len(right)
                        and not edge
                        and all(size_fit(indices[a], order[b]) >= 0.3 for a, b in zip(left, right))
                    ):
                        found += [(a, b, gap_score(indices[a], order[b]), "tight") for a, b in zip(left, right)]
                        continue
                    if edge:
                        # (past the first or last anchor only the nearest few: the rest may be another file's)
                        left, right = (left[-6:], right[-6:]) if a1 == -1 else (left[:6], right[:6])
                    # (Pro7 has more functions where 2.6 inlined its small statics: leaving one of those out costs
                    # little, a larger one or a row more)
                    loose = align_costed(
                        [indices[a] for a in left],
                        [order[b] for b in right],
                        lambda i, f: 0.4 + gap_score(i, f) if size_fit(i, f) > 0 else None,
                        lambda i: 0.6,
                        lambda f: 0.05 if f["static"] and len(f["tokens"]) < 90 else 0.5,
                    )
                    complete = (
                        not edge
                        and len(loose) == len(left)
                        and all(size_fit(indices[left[x]], order[right[y]]) >= 0.3 for x, y, _ in loose)
                    )
                    found += [(left[x], right[y], s, "gap" if complete else "loose") for x, y, s in loose]
                    for a in left:
                        gaps.setdefault(indices[a], []).extend(short(order[b]["name"]) for b in right)
                ranks = {"anchor": 3, "tight": 2, "gap": 1, "loose": 0}
                for a, b, s, how in found:
                    i, f = indices[a], order[b]
                    current = entries[i].get("order")
                    if not current or (ranks[how], s) > (ranks[current["how"]], current["score"]):
                        entries[i]["order"] = dict(
                            name=short(f["name"]),
                            file=file,
                            score=round(s, 2),
                            direction=direction,
                            how=how,
                            fit=round(size_fit(i, f), 2),
                        )
        for i, e in enumerate(entries):
            o = e.get("order")
            if o:
                if o["how"] == "anchor":
                    strength = "medium" if o["score"] >= 3 else "weak"
                elif o["how"] in ("tight", "gap"):
                    strength = "medium" if o["fit"] >= 0.3 else "weak"
                else:
                    strength = "weak"
                if o["name"] != short(e["name"]) or o["how"] != "anchor":
                    e["evidence"].append(
                        dict(
                            kind="order",
                            name=o["name"],
                            file=o["file"],
                            strength=strength,
                            detail=f"{o['how']} {o['direction']} {o['score']}",
                        )
                    )
            if i in gaps and not (o and o["name"] == short(e["name"])):
                e["gap_candidates"] = list(dict.fromkeys(gaps[i]))[:8]
        for e in entries:
            judge(e)
        changed = 0
        for i, e in enumerate(entries):
            name = e.get("proposed") if e["verdict"] == "rename" else short(e["name"])
            if name and name != effective[i]:
                effective[i] = name
                changed += 1
        print(f"round {sweep_round}: {changed} names changed", file=sys.stderr)
        if not changed:
            break


def judge(e):
    """The row's verdict: each name's support, its evidence kinds' strengths added (strong 3, medium 2, weak 1; the
    kinds are independent, so two that agree outweigh one)."""
    plain = short(e["name"])
    support = collections.Counter()
    best_kind = {}
    for ev in e["evidence"]:
        if ev["kind"] == "inlined":
            continue
        key = (ev["name"], ev["kind"])
        best_kind[key] = max(best_kind.get(key, 0), STRENGTHS.index(ev["strength"]) + 1)
    for (name, _), v in best_kind.items():
        support[name] += v
    strong = {ev["name"] for ev in e["evidence"] if ev["strength"] == "strong"}
    real = bool(set(e["provenance"]) & set("PWM"))
    unnamed = e["name"].startswith("fn_")
    # (Wii is later than 2.6, Pro7 earlier: a Wii name for a function whose row has its unit's Pro7 name is a later
    # rename -- Pro7's PPCLibImport_GetDropInFlags is Wii's CWPlugin_GetDropInFlags)
    own_unit = unit_key(e["source"]) if e.get("source") else None
    if own_unit and any(unit_key(f) == own_unit for f in e.get("pro7_files", [])):
        for name in [n for n in support if n != plain]:
            if {k for (n, k) in best_kind if n == name} == {"wii"}:
                e["note"] = f"Wii names it `{name}`"
                support.pop(name)
        support[plain] = max(support.get(plain, 0), 1)
    mine = support.get(plain, 0)
    others = [(v, n) for n, v in support.items() if n != plain]
    top, rival = max(others) if others else (0, None)
    if any(ev["kind"] == "library" for ev in e["evidence"]):
        result = "confirmed"  # (the library's own source; Wii's later MSL renamed some: __msl_read)
    elif any(ev["kind"] == "body" and ev["strength"] == "strong" for ev in e["evidence"]) and not any(
        ev["kind"] in ("assert", "assertpos", "strings") and ev["strength"] == "strong" and ev["name"] != plain
        for ev in e["evidence"]
    ):
        # (the code is Pro7's function of that name, byte for byte: only asserts or strings outrank it -- Wii pairs
        # twins by their calls)
        result = "confirmed"
    elif mine >= 3 and plain in strong and top < 3:
        result = "confirmed"
    elif mine >= 2 and top < 2:
        result = "likely"
    elif top >= 3 and mine >= 3:
        result = "conflict"
        e["proposed"] = rival
    elif top >= 3 or top >= 2 and (unnamed or not real or mine == 0) and mine < 2:
        result = "rename"
        e["proposed"] = rival
    elif top >= 2:
        result = "check"
        e["proposed"] = rival
    elif unnamed:
        result = "unnamed"
    elif real:
        result = "plausible"
    else:
        result = "unconfirmed"
    if top == 1 and result in ("unnamed", "unconfirmed", "plausible"):
        e["hint"] = rival
    if (
        result in ("plausible", "likely")
        and e.get("pro7_files")
        and e.get("source")
        and unit_key(e["source"]) not in {unit_key(f) for f in e["pro7_files"]}
    ):
        result = "check"
        e["note"] = "the name's Pro7 file is another unit"
    e["support"] = dict(support)
    e["verdict"] = result


def final_name(e):
    """The row's name once the list is applied: the review's, else a proposed rename, else its own."""
    if e.get("reviewed"):
        return e["reviewed"].get("name") or short(e["name"])
    if e["verdict"] == "rename" and e.get("proposed"):
        return e["proposed"]
    return e["name"] if e["name"].startswith("?") else short(e["name"])  # (C++ methods: one short name, many classes)


def final_unit(e):
    return (e.get("reviewed") or {}).get("unit") or e["source"]


def longest_monotone(points):
    """The longest run of POINTS [(row, line)] (in address order) whose lines only ascend: indexes into POINTS."""
    previous = [None] * len(points)
    tails, tail_index = [], []
    for k, (_, line) in enumerate(points):
        j = bisect.bisect_left(tails, line)
        previous[k] = tail_index[j - 1] if j else None
        if j == len(tails):
            tails.append(line)
            tail_index.append(k)
        else:
            tails[j] = line
            tail_index[j] = k
    best, k = [], tail_index[-1] if tail_index else None
    while k is not None:
        best.append(k)
        k = previous[k]
    return best[::-1]


def consistency(entries, pro7_by_name):
    """Checks over the list as a whole: a name given to several rows; rows whose (final) Pro7 name sits out of the
    order their unit's other Pro7 names follow (forward, or reversed for a deferred unit); each unit's direction
    against its build flags (`-inline auto,deferred` or `#pragma defer_codegen`)."""
    by_name = collections.defaultdict(list)
    for e in entries:
        by_name[final_name(e)].append(e)
    duplicates = {n: [e["address"] for e in es] for n, es in by_name.items() if len(es) > 1}
    sources = config.sources()
    units = collections.defaultdict(list)
    for e in entries:
        units[final_unit(e)].append(e)
    out_of_order, unit_rows = [], []
    for unit, group in units.items():
        placed = []
        for e in sorted(group, key=lambda e: int(e["address"], 16)):
            candidates = pro7_by_name.get(final_name(e), [])
            if len(candidates) == 1:
                placed.append((e, candidates[0]))
        files = collections.Counter(stem(f["file"]) for _, f in placed)
        if not files:
            continue
        home = files.most_common(1)[0][0]
        points = [(e, f["line"]) for e, f in placed if stem(f["file"]) == home]
        if len(points) < 3:
            continue
        forward = longest_monotone(points)
        backward = longest_monotone([(e, -line) for e, line in points])
        reversed_ = len(backward) > len(forward)
        keep = set(backward if reversed_ else forward)
        # (a unit with no order, like IroPointerAnalysis' included ADTs of inline functions, each emitted after its
        # first user, says nothing about single rows)
        ordered = len(keep) >= 0.6 * len(points)
        for k, (e, line) in enumerate(points):
            if k not in keep and ordered:
                e["out_of_order"] = f"{home}.c:{line}"
                out_of_order.append(e)
        flags = " ".join(sources[unit].flags) if unit in sources else ""
        path = project.ROOT / unit if unit else None
        pragma = bool(path) and path.exists() and "defer_codegen" in path.read_text(errors="replace")
        unit_rows.append(
            dict(
                unit=unit,
                home=home,
                placed=len(points),
                kept=len(keep),
                reversed=reversed_,
                ordered=ordered,
                deferred="deferred" in flags or pragma,
            )
        )
    return dict(
        duplicates=duplicates, out_of_order=out_of_order, units=sorted(unit_rows, key=lambda u: u["unit"] or "")
    )


VERDICT_ORDER = [
    "reviewed",
    "open",
    "rename",
    "conflict",
    "check",
    "unnamed",
    "unconfirmed",
    "plausible",
    "likely",
    "confirmed",
    "ok",
]
KIND = {
    "inlined": "inlined asserts",
    "library": "library",
    "assert": "asserts",
    "assertpos": "assert line",
    "strings": "strings",
    "wii": "Wii",
    "order": "order",
    "body": "body",
}


def write_report(entries, review, version, path, checks):
    """The list: per unit (in link order) its bounds' evidence and every row that is not confirmed."""

    def describe(e):
        parts = []
        for ev in e["evidence"]:
            if ev["kind"] in ("body", "library") and ev["name"] == short(e["name"]):
                continue
            where = Path(ev["file"]).name if ev.get("file") else ev.get("module", "")
            parts.append(f"{KIND[ev['kind']]} {ev['strength'][0]}: `{ev['name']}` ({where}; {ev['detail']})")
        if e.get("gap_candidates"):
            parts.append("Pro7 gap: " + ", ".join(f"`{n}`" for n in e["gap_candidates"][:6]))
        if e.get("note"):
            parts.append(e["note"])
        if e.get("reviewed", {}).get("note"):
            parts.append("**review**: " + e["reviewed"]["note"])
        return "; ".join(parts) or "-"

    counts = collections.Counter(e["verdict"] for e in entries)
    lines = [
        f"# {version}: function names and unit bounds",
        "",
        "Generated by `tools/evidence.py sweep` (after `tools/evidence.py pair`; decisions from reading the code",
        f"are in `config/{version}/name_review.json` and override the evidence). Every row of `functions.json` is",
        "checked against Pro7 (Ninji's decompilation), the Wii compilers' CodeView (names and modules), MSL, and",
        "2.6's own assert files and lines. A name only the gc1 decompilation has, or nobody has, was made up by an",
        "earlier pass: unconfirmed, not necessarily wrong (2.x has functions Pro7 and Wii lack).",
        "",
        "Verdicts: **reviewed** (a decision from reading the code), **open** (read, still undecided), **rename** "
        "(the evidence names it otherwise),",
        "**conflict** (strong evidence disagrees), **check** (medium evidence names it otherwise, or its name's",
        "Pro7 file is another unit), **unnamed** (fn_), **unconfirmed** (an invented name, nothing names it),",
        "**plausible** (a real name, unconfirmed), **likely** / **confirmed** (named so by medium / strong",
        "evidence; not listed per row). Evidence: asserts (Pro7 function with the same assert lines), assert",
        "line (one line at the neighbours' offset), strings (Pro7 literals few functions share), Wii (paired",
        "code; strength by similarity and method), order (aligned with the Pro7 file in order, or reversed for",
        "deferred units: anchor / tight gap / gap / loose), body (the source's body is Pro7's, exact); s/m/w =",
        "strong / medium / weak. Pro7 gap: the Pro7 functions between the aligned neighbours.",
        "",
        "| verdict | rows |",
        "|---|---|",
    ]
    lines += [f"| {v} | {counts[v]} |" for v in VERDICT_ORDER if counts[v]]
    lines += ["", "## Unit bounds", ""]
    lines += [f"- `{u}`: {d}" for u, d in sorted(review.get("units", {}).items())] or ["- (none reviewed yet)"]
    by_address = {e["address"]: e for e in entries}
    lines += [
        "",
        "## Names on several rows",
        "",
        "Once the list is applied (review, else a proposed rename, else the row's name), these names would be",
        "given to more than one row: statics of different units with one name need a unit prefix (functions.json",
        "names are global), anything else is a mistake to settle.",
        "",
    ]
    lines += [
        f"- `{n}`: " + ", ".join(f"{a} (`{by_address[a]['name']}`, {final_unit(by_address[a])})" for a in addresses)
        for n, addresses in sorted(checks["duplicates"].items())
    ] or ["- (none)"]
    lines += [
        "",
        "## Out of Pro7's order",
        "",
        "Rows whose final name is a Pro7 function of their unit's file but out of the order the unit's other",
        "Pro7 names follow (the longest ascending, or for deferred units descending, run of Pro7 lines in",
        "address order). Each is a name to re-check, confirmed and exact rows included: a body matched under",
        "a lookalike's name, or a function filed in the wrong unit. Where the Pro7 file is not the unit's own",
        "(ObjGen_PPC_EABI against ObjGenMachO, MsDos against Posix) the names are by analogy and the order",
        "need not agree; `CW_INLINE` helpers' copies follow their first user (CLIO).",
        "",
        "| address | row | final name | Pro7 | verdict | unit |",
        "|---|---|---|---|---|---|",
    ]
    lines += [
        f"| {e['address']} | `{e['name']}` | `{final_name(e)}` | {e['out_of_order']} | {e['verdict']} | "
        f"{final_unit(e)} |"
        for e in checks["out_of_order"]
    ]
    lines += [
        "",
        "## Deferred units",
        "",
        "A unit whose functions run in reverse of Pro7's file was built `-inline auto,deferred`. Units the repo",
        "builds without it are written in the binary's order instead, which matches only while no function is",
        "inlined into one defined before it (deferred inlining can, `-inline auto` cannot): those near-misses",
        "need Pro7's order and the flag.",
        "",
        "| unit | Pro7 file | named rows in order | original | repo |",
        "|---|---|---|---|---|",
    ]
    for u in checks["units"]:
        order = "no order" if not u["ordered"] else "reversed (deferred)" if u["reversed"] else "forward"
        repo = "deferred" if u["deferred"] else "not deferred"
        mark = " **(to change)**" if u["ordered"] and u["reversed"] != u["deferred"] else ""
        lines.append(f"| {u['unit']} | {u['home']}.c | {u['kept']}/{u['placed']} | {order} | {repo}{mark} |")
    lines += ["", "## Units", ""]
    for source, group in itertools.groupby(entries, key=lambda e: e["source"]):
        group = list(group)
        homes = collections.defaultdict(collections.Counter)
        for e in group:
            for k, v in e["homes"].items():
                homes[k][v] += 1
        tally = collections.Counter(e["verdict"] for e in group)
        lines.append(
            f"### {source or '(no source)'} at {group[0]['address']} ({len(group)} rows: "
            + ", ".join(f"{tally[v]} {v}" for v in VERDICT_ORDER if tally[v])
            + ")"
        )
        lines.append("")
        if homes:
            lines.append(
                "Homes: "
                + "; ".join(
                    f"{k} " + ", ".join(f"{v} {n}" for v, n in c.most_common()) for k, c in sorted(homes.items())
                )
            )
            lines.append("")
        listed = [e for e in group if e["verdict"] not in ("confirmed", "likely", "ok")]
        if listed:
            lines += ["| address | row | verdict | name | evidence |", "|---|---|---|---|---|"]
            for e in listed:
                name = e.get("proposed") or (f"? {e['hint']}" if e.get("hint") else "")
                if e.get("reviewed", {}).get("unit"):
                    name += f" (unit: {e['reviewed']['unit']})"
                lines.append(
                    f"| {e['address']} | `{e['name']}` | {e['verdict']} | {name and f'`{name}`'} | "
                    f"{describe(e)} |".replace("``", "")
                )
            lines.append("")
    project.write_atomic(path, "\n".join(lines) + "\n")


# --------------------------------------------------------------------------------------------------------- review


def review_addresses(specs, known):
    out = []
    for spec in specs:
        if not re.fullmatch(r"(0x)?[0-9a-fA-F]+(-(0x)?[0-9a-fA-F]+)?", spec):
            raise project.Error(f"{spec}: not an ADDRESS or A-B (hexadecimal)")
        if "-" in spec:
            lo, hi = (int(x, 16) for x in spec.split("-"))
            out += [a for a in known if lo <= int(a, 16) <= hi]
        else:
            a = config.format_address(int(spec, 16))
            if a not in known:
                raise project.Error(f"{a}: no row there")
            out.append(a)
    return out


def review(version, command, args, note, unit):
    sweep_path = version.build / "name_sweep.json"
    if not sweep_path.exists():
        raise project.Error(f"{project.relative(sweep_path)} does not exist: python tools/evidence.py sweep")
    entries = json.loads(sweep_path.read_text())
    if command == "show":
        for e in entries:
            if not any(s in (e["source"] or "None") for s in args):
                continue
            evidence = "; ".join(
                f"{x['kind']}:{x['name']}({x['strength'][0]},{x['detail']})"
                for x in e["evidence"]
                if x["name"] != e["name"] or x["kind"] in ("assert", "assertpos", "wii", "strings")
            )
            module = f" [{e['wii']['module']}]" if e.get("wii") else ""
            proposed = e.get("proposed") or (f"?{e['hint']}" if e.get("hint") else "")
            gap = f" | gap: {','.join(e['gap_candidates'][:6])}" if e.get("gap_candidates") else ""
            files = f" | {','.join(e['assert_files'])}" if e.get("assert_files") else ""
            print(
                f"{e['address'][4:]} {'X' if e['exact'] else ' '} {e['size']:5} {e['verdict'][:6]:6} "
                f"{e['name'][:38]:38} {e['provenance']:4} {proposed[:34]:34}{module} | {evidence}{gap}{files}"
            )
        return
    # (the rows' current names, which each decision records as "was")
    current = {config.format_address(r.address): r for r in config.functions(version)}
    with project.Transaction() as tx:
        path = version.config / "name_review.json"
        text = tx.read_bytes(path)
        decisions = json.loads(text) if text is not None else {"functions": {}, "units": {}}

        def decide(address, name):
            row = current[address]
            decision = dict(name=name)
            if note:
                decision["note"] = note
            if unit and unit != row.source:
                decision["unit"] = unit
            decision["was"] = row.name
            decisions["functions"][address] = decision

        if command == "unit":
            if len(args) < 2:
                raise project.Error("unit SOURCE NOTE...")
            decisions["units"][args[0]] = " ".join(args[1:])
        elif command == "batch":
            try:
                batch = Path(args[0]).read_text().splitlines()
            except OSError as e:
                raise project.Error(f"{args[0]}: {e.strerror}") from None
            for line in batch:
                line = line.strip() if line.lstrip().startswith("0x") else line.split("#", 1)[0].strip()
                if not line:
                    continue
                spec, _, line_note = line.partition("|")
                if len(spec.split()) < 2:
                    raise project.Error(f"{args[0]}: {line!r} is not ADDRESS NAME [| NOTE]")
                address, name = spec.split()[:2]
                address = review_addresses([address], current)[0]
                decide(address, current[address].name if name == "=" else name)
                if line_note.strip():
                    decisions["functions"][address]["note"] = line_note.strip()
        elif command == "set":
            if len(args) != 2:
                raise project.Error("set ADDRESS NAME")
            decide(review_addresses(args[:1], current)[0], args[1])
        else:
            by_address = {e["address"]: e for e in entries}
            for address in review_addresses(args, current):
                if command == "drop":
                    decisions["functions"].pop(address, None)
                elif command == "accept":
                    e = by_address[address]
                    name = e.get("auto", {}).get("proposed") or e.get("proposed")
                    if e["verdict"] in ("unnamed", "unconfirmed", "plausible") and not name:
                        name = e.get("hint")
                    decide(address, name or current[address].name)
                else:
                    decide(address, current[address].name)
        decisions["functions"] = dict(sorted(decisions["functions"].items()))
        tx.write(path, json.dumps(decisions, indent=2) + "\n")


# ---------------------------------------------------------------------------------------------------------- apply


def apply(version, write, only, force):
    rows = {config.format_address(r.address): r.name for r in config.functions(version)}
    mapping, stale = {}, []
    for address, decision in read_review(version)["functions"].items():
        name, row = decision["name"], rows.get(address)
        if (decision.get("note") or "").startswith("OPEN") or "::" in name or row is None or row == name:
            continue
        if row != decision.get("was"):
            stale.append(f"{address}: the decision renames {decision.get('was')} to {name}; the row is {row} now")
            continue
        mapping[row] = name
    for line in stale:
        print(f"stale: {line}")
    if only:
        mapping = {o: n for o, n in mapping.items() if o in only or n in only}
    held = {}
    texts = renaming.scan(project.Transaction(), set(mapping) | set(mapping.values()))
    for new, sites in renaming.taken_names(mapping, texts).items():
        old = next(o for o, n in mapping.items() if n == new)
        if new not in force and old not in force:
            held[old] = new
            print(f"held: {old} -> {new}")
            for s in sites[:4]:
                text = texts[s.path][0]
                print(f"    {project.relative(s.path)}:{s.line}: {text.splitlines()[s.line - 1].strip()[:100]}")
    for old in held:
        del mapping[old]
    # (a rename onto a name that a held one keeps waits too)
    kept = set(rows.values()) - set(mapping)
    while blocked := [o for o, n in mapping.items() if n in kept]:
        for o in blocked:
            print(f"held: {o} -> {mapping.pop(o)} (its new name is still another row's)")
        kept = set(rows.values()) - set(mapping)
    print(f"{len(mapping)} renames")
    if not mapping:
        return
    plan = renaming.rename(mapping, force=set(mapping.values()))
    if plan.problems:
        raise project.Error("\n".join(plan.problems))
    if write:
        plan.tx.commit()
    print("\n".join(plan.changes))


# ------------------------------------------------------------------------------------------------- unit and units


def unit_view(version, where):
    from status import text_ranges

    units = text_ranges(version)
    if len(where) == 2 and all(re.fullmatch(r"(0x)?[0-9a-fA-F]+", w) for w in where):
        start, end = (int(w, 16) for w in where)
        chosen = [u for u in units if u[1] < end and u[2] > start]
    else:
        chosen = [u for u in units if where[0] in u[0]]
        if not chosen:
            raise project.Error(f"no unit matches {where[0]}")
        start, end = min(u[1] for u in chosen), max(u[2] for u in chosen)
    pe = original(version)
    rows = config.functions(version)
    results = verdict.load(version)
    targets = asserts.targets(rows)
    names_wii = wii_names()
    sources = config.sources()
    current = None
    for r in rows:
        if not start <= r.address < end:
            continue
        unit = next((u for u, s, e in units if s <= r.address < e), "?")
        if unit != current:
            current = unit
            if unit in sources:
                c = build.compilation(version, unit)
                print(f"\n== {unit}  [{c.compiler} {' '.join(c.flags)}]")
            else:
                print(f"\n== {unit}  [split]")
        v = results.get(r.address)
        state = "none" if v is None else "EXACT" if v.exact else f"{100 * v.similarity:5.1f}"
        _, found = asserts.sites(pe, pe.read(r.address, r.size), r.address, targets)
        files = collections.defaultdict(set)
        for s in found:
            files[s.file].add(s.line)
        sites = " ".join(f"{f}:{','.join(map(str, sorted(l)))}" for f, l in files.items())
        mark = f"=wii({names_wii[short(r.name)]})" if short(r.name) in names_wii else ""
        other = "" if r.source in (unit, None) else f"[{Path(r.source).name}]"
        print(f"  {r.address:06x} {r.size:6}  {state:>6}  {r.name}{mark} {other} {sites}".rstrip())


def units_table(version, pro7_root, output):
    from status import survey

    table, _, _ = survey(version)
    pe = original(version)
    rows = config.functions(version)
    targets = asserts.targets(rows)
    image_modules = set()
    names_wii = {}
    for exe in wii_executables():
        image = wii.read(exe)
        image_modules |= {c.module.lower() for c in image.contributions}
        for address, name in image.names().items():
            names_wii.setdefault(name, image.module_at(address))
    if not Path(pro7_root).is_dir():
        raise project.Error(f"{pro7_root} does not exist (--pro7 DIR: Ninji's Pro 7 decompilation)")
    pro7_stems = {unit_key(p.name) for p in Path(pro7_root).rglob("*.c*")}
    out = [
        f"# {version}: translation units (generated by tools/evidence.py units)",
        "",
        "Columns: see `tools/evidence.py`. `order` is read from the original's own assert lines along each unit's "
        ".text; `rev` units were built `-inline auto,deferred` (reverse source order).",
        "",
        "| unit | range | fns | bytes | host | order | name | link |",
        "|---|---|---|---|---|---|---|---|",
    ]
    for t in table:
        unit, start, end = t["unit"], t["start"], t["end"]
        key = unit_key(unit)
        inside = [r for r in rows if start <= r.address < end]
        lines, named = [], set()
        for r in inside:
            _, found = asserts.sites(pe, pe.read(r.address, r.size), r.address, targets)
            if any(unit_key(s.file) == key for s in found):
                named.add("assert")
            # (the first line each asserting function pushes for the unit's own file, in address order: an #included
            # .c or a header has lines of its own)
            own = [s.line for s in found if unit_key(s.file) == key and not s.file.lower().endswith(".h")]
            if own:
                lines.append(min(own) if not lines else min(own, key=lambda l: abs(l - lines[-1])))
        if len(lines) < 3:
            order = "-"
        else:
            down = sum(1 for a, b in zip(lines, lines[1:]) if b < a)
            up = sum(1 for a, b in zip(lines, lines[1:]) if b > a)
            order = "rev" if down >= 3 * max(up, 1) else "asc" if up >= 3 * max(down, 1) else "mixed"
        # (a Wii module of the name: by its own publics here, or only by name -- the Wii codebase is later, but it
        # keeps the file names)
        if any(unit_key(names_wii.get(r.name, "") + ".c") == key for r in inside):
            named.add("wii")
        elif key in {unit_key(m + ".c") for m in image_modules}:
            named.add("wii-name")
        if key in pro7_stems:
            named.add("pro7")
        host = t["compiler"] + (
            " " + t["flags"].replace("-O4,p ", "").replace("-Cpp_exceptions", "exc") if t["flags"] else ""
        )
        out.append(
            f"| {unit} | {start:06x}-{end:06x} | {t['exact']}/{t['total']} | {t['bytes']:.0f}% | {host} | "
            f"{order} | {','.join(sorted(named)) or '?'} | {t['link']} |"
        )
    path = Path(output) if output else project.ROOT / f"docs/units-{version_doc_name(version)}-table.md"
    project.write_atomic(path, "\n".join(out) + "\n")
    print(project.relative(path))


def align_view(version, unit, reverse, pro7_root):
    if not Path(pro7_root).is_dir():
        raise project.Error(f"{pro7_root} does not exist (--pro7 DIR: Ninji's Pro 7 decompilation)")
    reference = []
    found = [p for p in Path(pro7_root).rglob(f"{unit}.c")]
    if len(found) > 1:
        raise project.Error(
            f"{unit}.c is in {len(found)} places in {pro7_root}: "
            + ", ".join(str(p.relative_to(pro7_root)) for p in found)
        )
    for f in cindex.functions(found[0]) if found else []:
        reference.append(("s " if f["static"] else "  ") + f["name"])
    rows = sorted(
        (r for r in config.functions(version) if r.source and Path(r.source).stem == unit),
        key=lambda r: r.address,
        reverse=reverse,
    )
    ours = [f"{r.address:06x} {r.size:5} {r.name}" for r in rows]
    if not found:
        print(f"(no {unit}.c in {pro7_root})")
    for i in range(max(len(ours), len(reference))):
        print(f"{ours[i] if i < len(ours) else '':60} | {reference[i] if i < len(reference) else ''}")


def main():
    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)  # (piped into head: stop quietly)
    parser = argparse.ArgumentParser(
        prog="tools/evidence.py",
        usage="%(prog)s [--version V] pair|sweep|review|apply|unit|units|align ...",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("--version", default=project.PRIMARY, help=f"default {project.PRIMARY}")
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    p = commands.add_parser("pair")
    p.add_argument("--exe", default=wii.DEFAULT)
    p = commands.add_parser("sweep")
    p.add_argument("--pro7", default=PRO7)
    p.add_argument("--gc1", default=GC1)
    p.add_argument("-o", "--output")
    p = commands.add_parser("review")
    p.add_argument("action", choices=["show", "accept", "set", "ok", "unit", "drop", "batch"])
    p.add_argument("args", nargs="+")
    p.add_argument("-n", "--note")
    p.add_argument("-u", "--unit")
    p = commands.add_parser("apply")
    p.add_argument("--write", action="store_true")
    p.add_argument("--only", nargs="+")
    p.add_argument("--force", nargs="+", default=[])
    p = commands.add_parser("unit")
    p.add_argument("where", nargs="+")
    p = commands.add_parser("units")
    p.add_argument("--pro7", default=PRO7)
    p.add_argument("-o", "--output")
    p = commands.add_parser("align")
    p.add_argument("unit")
    p.add_argument("--rev", action="store_true")
    p.add_argument("--pro7", default=PRO7)
    # (--version may come after the command too)
    for sub in commands.choices.values():
        sub.add_argument("--version", default=argparse.SUPPRESS)
    args = parser.parse_args()
    try:
        version = project.version(args.version)
        if args.command == "pair":
            pair(version, Path(args.exe))
        elif args.command == "sweep":
            sweep(version, args.pro7, args.gc1, args.output)
        elif args.command == "review":
            review(version, args.action, args.args, args.note, args.unit)
        elif args.command == "apply":
            apply(version, args.write, args.only, set(args.force))
        elif args.command == "unit":
            unit_view(version, args.where)
        elif args.command == "units":
            units_table(version, args.pro7, args.output)
        else:
            align_view(version, args.unit, args.rev, args.pro7)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
