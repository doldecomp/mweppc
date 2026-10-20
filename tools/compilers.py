#!/usr/bin/env python3

import argparse
import collections
import fnmatch
import signal
import sys
from dataclasses import dataclass, replace
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, config, csrc, isolated, project, verdict
from mw.coff import Object
from mw.pe import original


@dataclass(frozen=True)
class Candidate(build.Compilation):
    path: str = ""

    @property
    def executable(self) -> str:
        return self.path


def candidate(compilation: build.Compilation, name: str) -> build.Compilation:
    if "/" in name or name.endswith(".exe"):
        return Candidate(compilation.version, compilation.source, "", compilation.args, str(Path(name).resolve()))
    if not (project.ROOT / "build/compilers" / name / "mwcc.exe").exists():
        raise project.Error(f"no compiler build/compilers/{name}/mwcc.exe")
    return replace(compilation, compiler=name)


def with_flags(compilation: build.Compilation, replacements: list[str]) -> build.Compilation:
    args = " ".join(compilation.args)
    for r in replacements:
        if "=" not in r:
            raise project.Error(f"--replace {r}: expected OLD=NEW")
        old, new = r.split("=", 1)
        args = args.replace(old, new)
    return replace(compilation, args=tuple(args.split()))


def selected(version, pattern: str) -> list[str]:
    sources = [s for s in config.sources() if fnmatch.fnmatch(s, pattern)]
    if not sources:
        raise project.Error(f"no source matches {pattern}")
    return sources


def data_agreement(obj: Object, addresses: dict[str, int], pe) -> tuple[int, int]:
    data = next(s for s in pe.sections if s.name == ".data")
    bss = next(s for s in pe.sections if s.name == ".bss")
    agree = disagree = 0
    for s in obj.symbols:
        if not s.external or s.name not in addresses:
            continue
        if s.common:
            mine = ".bss"
        elif s.defined and s.section > 0 and not obj.section(s.section).code:
            mine = ".bss" if obj.section(s.section).bss else ".data"
        else:
            continue
        address = addresses[s.name]
        theirs = (
            ".data"
            if data.virtual_address <= address < data.virtual_address + data.virtual_size
            else ".bss"
            if bss.virtual_address <= address < bss.virtual_address + bss.virtual_size
            else None
        )
        if theirs:
            agree += mine == theirs
            disagree += mine != theirs
    return agree, disagree


def host(args):
    v = project.version(args.version)
    names = args.compilers.split(",")
    sources = selected(v, args.sources)
    table = verdict.rows(v)
    addresses = verdict.addresses(v, table)
    pe = original(v)
    configured = {s: build.compilation(v, s) for s in sources}
    jobs = [(s, name) for s in sources for name in ["(configured)", *names]]

    def run(job):
        source, name = job
        compilation = with_flags(configured[source], args.replace)
        if name != "(configured)":
            compilation = candidate(compilation, name)
        result = workspace.compile(compilation)
        if not result.ok:
            return None
        verdicts = verdict.compare_file(v, source, result.object)
        place = data_agreement(Object.read(result.object), addresses, pe)
        result.object.unlink()
        return {x.address: x for x in verdicts}, place

    with isolated.Workspace() as workspace:
        results = dict(zip(jobs, isolated.parallel(run, jobs)))
    width = max(17, *(len(n) + 1 for n in names))
    print(
        f"{'source':<44} {'cfg':<6} "
        + " ".join(f"{n[-width:]:>{width}}" for n in names)
        + "  (exact/total similarity% data agree/disagree)"
    )
    gained, lost = collections.defaultdict(list), collections.defaultdict(list)
    for source in sources:
        cells, scores = [], {}
        base = results[(source, "(configured)")]
        for name in names:
            got = results[(source, name)]
            if got is None:
                cells.append("fail")
                continue
            verdicts, (agree, disagree) = got
            exact = sum(x.exact for x in verdicts.values())
            similar = sum(1.0 if x.exact else x.similarity for x in verdicts.values()) / max(len(verdicts), 1)
            scores[name] = (exact, -disagree, similar)
            cells.append(f"{exact}/{len(verdicts)} {100 * similar:3.0f} {agree}/{disagree}")
            if base:
                for address, x in verdicts.items():
                    was = base[0].get(address)
                    if was and x.exact and not was.exact:
                        gained[(source, name)].append(x.name)
                    elif was and was.exact and not x.exact:
                        lost[(source, name)].append(x.name)
        best = max(scores, key=scores.get, default=None)
        cells = [("*" if name == best else "") + cell for name, cell in zip(names, cells)]
        print(f"{source[:44]:<44} {configured[source].compiler:<6} " + " ".join(f"{c:>{width}}" for c in cells))
    for key in sorted(gained):
        source, name = key
        if name == configured[source].compiler:
            continue
        print(
            f"  {source} with {name}: +{len(gained[key])} {' '.join(gained[key][:8])}"
            + (f"; -{len(lost[key])} {' '.join(lost[key][:8])}" if lost[key] else "")
        )


def deferred_compilation(compilation: build.Compilation) -> build.Compilation:
    args = list(compilation.args)
    if "-inline" in args:
        args[args.index("-inline") + 1] = "auto,deferred"
    else:
        args[1:1] = ["-inline", "auto,deferred"]
    return replace(compilation, args=tuple(args))


def kept_order(obj: Object, rows: list[verdict.Row]) -> float:
    position = {}
    for s in sorted(
        (s for s in obj.symbols if s.defined and s.function and s.section > 0), key=lambda s: (s.section, s.value)
    ):
        position.setdefault(s.name, len(position))
    names = [r.symbol if r.symbol.startswith("?") else "_" + r.symbol for r in sorted(rows, key=lambda r: r.address)]
    pairs = [(a, b) for a, b in zip(names, names[1:]) if a in position and b in position]
    return sum(position[a] < position[b] for a, b in pairs) / len(pairs) if pairs else 1.0


def first_error(log: str) -> str:
    lines = log.splitlines()
    for i, line in enumerate(lines[:-1]):
        if "Error:" in line:
            return lines[i + 1].lstrip("# ").strip()
    return log.strip().splitlines()[-1] if log.strip() else "?"


def deferred(args):
    v = project.version(args.version)
    sources = selected(v, args.sources)
    table = verdict.rows(v)

    def run(source):
        rows = [r for r in table if r.source == source]
        now = build.compilation(v, source)
        out = []
        for compilation, reorder in ((now, False), (deferred_compilation(now), True)):
            texts = [None]
            if reorder:
                try:
                    code = csrc.Source(source, version=v)
                    functions = code.placed({r.name: r.address for r in rows})
                    text, _ = code.ordered(functions, descending=True)
                except project.Error as e:
                    out.append(str(e).split("\n")[0])
                    continue
                at = min((code.extent(d)[0] for _, d in functions), default=0)
                prototypes = "".join(
                    f"{' '.join(code.text[d.start : d.body].split())};\n"
                    for d in code.definitions
                    if d.storage == "static" and d.start >= at
                )
                texts = [text, text[:at] + prototypes + "\n" + text[at:]] if prototypes else [text]
            for i, text in enumerate(texts):
                result = workspace.compile(compilation, text)
                if result.ok:
                    break
            if not result.ok:
                out.append("fail: " + first_error(result.log))
                continue
            obj = Object.read(result.object)
            exact = sum(x.exact for x in verdict.compare_file(v, source, result.object, score=False))
            result.object.unlink()
            out.append((exact, len(rows), kept_order(obj, rows), i > 0))
        return source, out

    print(f"{'source':<44} {'now':>9} {'order':>6}   {'deferred':>9} {'order':>6}")
    with isolated.Workspace() as workspace:
        for source, (now, then) in isolated.parallel(run, sources):

            def cell(x):
                if not isinstance(x, tuple):
                    return f"  {x}"
                return f"{x[0]:>4}/{x[1]:<4} {100 * x[2]:5.0f}%" + (" +prototypes" if x[3] else "")

            mark = ""
            if isinstance(now, tuple) and isinstance(then, tuple):
                mark = "  better" if then[0] > now[0] else "  worse" if then[0] < now[0] else ""
            print(f"{source[:44]:<44} {cell(now)}   {cell(then)}{mark}", flush=True)


def main():
    if hasattr(signal, "SIGPIPE"):
        signal.signal(signal.SIGPIPE, signal.SIG_DFL)
    parser = argparse.ArgumentParser(
        description="How each unit was built, from trial compiles.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--version")
    common.add_argument("--sources", default="*", help="a glob of config/sources.json's paths")
    commands = parser.add_subparsers(dest="command", required=True)
    p = commands.add_parser("host", parents=[common])
    p.add_argument("--compilers", default="pro5,pro53,pro6", help="the candidates, comma-separated")
    p.add_argument("--replace", action="append", default=[], metavar="OLD=NEW")
    commands.add_parser("deferred", parents=[common])
    args = parser.parse_args()
    try:
        host(args) if args.command == "host" else deferred(args)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
