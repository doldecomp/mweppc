#!/usr/bin/env python3
"""python tools/equivalence.py CORPUS.json OUTDIR [--version V | ORIGINAL CANDIDATE]: compile a corpus with the
original compiler and a candidate, and compare the objects byte for byte. By default the candidate is the clang build
of V (default GC_2_6: build/V/clang/mwcceppc.exe, brought up to date with ninja first) and the original V's.

CORPUS.json: {"root": DIR, "jobs": [{"source": PATH, "cflags": [...], "sjis": bool}, ...]}: each source compiled in
ROOT with its flags, through sjiswrap (ROOT/build/tools/sjiswrap.exe) when "sjis". Each job's outputs are
OUTDIR/{original,candidate}/<job number>.o; OUTDIR/results.json has every job's result. A difference is a behaviour
of the original the candidate does not have. Exits 1 unless every job's objects are identical: a job the original
fails tests nothing, and that is a failure of the corpus.

The two bytes of struct padding in some C++ exception actions (a catch block's, for one) are the host's stack garbage
in the original (tools/mw/extab.py): both sides' are set to the same bytes before comparing, as a decomp-toolkit
project's `dtk extab clean --padding` does to its objects. --extab-padding HEX gives them (default 0000; a job's own
"extab_padding": HEX, which `corpus` takes from the project, wins over it), and --extab-padding raw compares the
objects as written. Both sides get the same bytes, so which bytes only changes that count, never the verdict. The
objects are kept as the compilers wrote them; a job identical only with the padding set has the number of bytes that
took ("extab padding") in its result, and a table that does not parse makes the job different, with why in its log.

The compilers run under $WIBO (default build/tools/wibo). sjiswrap needs a wibo that can load it: a decomp-toolkit
project's own (WIBO=PROJECT/build/tools/wibo).

tools/equivalence.py corpus PROJECT MW_VERSION OUT.json: a corpus of the sources a decomp-toolkit project
builds with MW_VERSION (as its build.ninja names it, e.g. GC/2.6): one job per source, with the extab padding the
project gives the object (its build.ninja's extab_padding) where it gives one.
"""

import argparse
import concurrent.futures
import json
import os
import shlex
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, config, extab, project


def compile_one(root, compiler, job, output):
    command = build.windows_prefix()
    if job.get("sjis"):
        command.append(str(Path(root) / "build/tools/sjiswrap.exe"))
    command += [str(compiler), *job["cflags"], "-c", job["source"], "-o", str(output)]
    output.unlink(missing_ok=True)
    try:
        result = subprocess.run(
            command,
            cwd=root,
            capture_output=True,
            text=True,
            errors="replace",
            timeout=900,
            env=dict(os.environ, MWCIncludes="."),
        )
    except subprocess.TimeoutExpired:
        return "timeout"
    if result.returncode or not output.exists():
        return (result.stdout + result.stderr)[-600:] or f"exit status {result.returncode}"
    return None


def same(a: bytes, b: bytes, padding: bytes | None) -> tuple[bool, int]:
    """Whether objects A and B are the same with their exception actions' padding set to PADDING (None: as written),
    and how many bytes that took; ExtabError when a table of either does not parse."""
    if a == b or padding is None:
        return a == b, 0
    (a, changed_a), (b, changed_b) = extab.normalize(a, padding), extab.normalize(b, padding)
    return a == b, changed_a + changed_b


def run(corpus_path, out, original, candidate, padding=b"\0\0"):
    try:
        corpus = json.loads(Path(corpus_path).read_text())
    except (OSError, ValueError) as e:
        raise project.Error(f"{corpus_path}: {e}") from None
    if (
        not isinstance(corpus, dict)
        or not isinstance(corpus.get("root"), str)
        or not isinstance(corpus.get("jobs"), list)
    ):
        raise project.Error(f'{corpus_path} is not {{"root": DIR, "jobs": [...]}}')
    for side in ("original", "candidate"):
        (out / side).mkdir(parents=True, exist_ok=True)
        for old in (out / side).glob("*.o"):
            old.unlink()

    def one(numbered):
        number, job = numbered
        a, b = out / "original" / f"{number}.o", out / "candidate" / f"{number}.o"
        result = dict(job=number, source=job["source"])
        failure = compile_one(corpus["root"], original, job, a)
        if failure:
            return dict(result, result="original fails", log=failure)
        failure = compile_one(corpus["root"], candidate, job, b)
        if failure:
            return dict(result, result="candidate fails", log=failure)
        job_padding = (
            padding if padding is None or "extab_padding" not in job else extab.parse_padding(job["extab_padding"])
        )
        try:
            identical, normalized = same(a.read_bytes(), b.read_bytes(), job_padding)
        except extab.ExtabError as e:
            return dict(result, result="different", log=f"extab: {e}")
        if not identical:
            return dict(result, result="different")
        return dict(result, result="identical", **({"extab padding": normalized} if normalized else {}))

    with concurrent.futures.ThreadPoolExecutor(int(os.environ.get("JOBS", 0)) or os.cpu_count() or 4) as pool:
        results = list(pool.map(one, enumerate(corpus["jobs"])))
    project.write_atomic(
        out / "results.json",
        json.dumps(
            dict(
                original=str(original),
                candidate=str(candidate),
                candidate_sha1=project.digest(Path(candidate).read_bytes()),
                results=results,
            ),
            indent=1,
        )
        + "\n",
    )
    counts = {}
    for r in results:
        counts[r["result"]] = counts.get(r["result"], 0) + 1
    for r in results:
        if r["result"] != "identical":
            print(
                f"{r['result']}: job {r['job']}, {r['source']}"
                + (f": {' '.join(r['log'].split())[-300:]}" if r.get("log") else "")
            )
    normalized = sum(1 for r in results if r.get("extab padding"))
    print(
        ", ".join(f"{n} {what}" for what, n in sorted(counts.items())),
        f"(of {len(results)}{f'; {normalized} identical only with the extab padding set' if normalized else ''};",
        f"{out}/results.json)",
    )
    return all(r["result"] == "identical" for r in results) and results


def dtk_corpus(root, mw_version, output):
    """The jobs of a decomp-toolkit project's build.ninja: every mwcc edge built with MW_VERSION."""
    root = Path(root).resolve()
    try:
        text = (root / "build.ninja").read_text().replace("$\n", "")
    except OSError as e:
        raise project.Error(f"{root}/build.ninja: {e.strerror} (configure the project first)") from None
    jobs, edge = [], None

    def flush():
        if edge and edge["vars"].get("mw_version") == mw_version:
            job = dict(source=edge["source"], cflags=shlex.split(edge["vars"]["cflags"]), sjis="sjis" in edge["rule"])
            # (decomp-toolkit's mwcc_extab rules: the object's padding, given to dtk extab clean)
            if edge["vars"].get("extab_padding"):
                job["extab_padding"] = extab.parse_padding(edge["vars"]["extab_padding"]).hex()
            jobs.append(job)

    for line in text.splitlines():
        if line.startswith("build "):
            flush()
            edge = None
            _, _, rest = line[6:].partition(":")
            fields = rest.split()
            if fields and fields[0].startswith("mwcc"):
                edge = dict(rule=fields[0], source=fields[1], vars={})
        elif line.startswith("  ") and edge is not None and " = " in line:
            key, _, value = line.strip().partition(" = ")
            edge["vars"][key] = " ".join(value.split())
        elif not line.startswith("  "):
            flush()
            edge = None
    flush()
    # (a source built twice under one version is one test)
    unique = list({job["source"]: job for job in jobs}.values())
    if not unique:
        raise project.Error(f"{root}/build.ninja has no mwcc edge with mw_version = {mw_version}")
    project.write_atomic(output, json.dumps(dict(root=str(root), jobs=unique), indent=1) + "\n")
    print(f"{len(unique)} jobs")


def main():
    args = sys.argv[1:]
    try:
        if args[:1] == ["corpus"]:
            if len(args) != 4:
                sys.exit("usage: python tools/equivalence.py corpus PROJECT MW_VERSION OUT.json")
            dtk_corpus(*args[1:])
            return
        parser = argparse.ArgumentParser(
            prog="tools/equivalence.py",
            usage="%(prog)s CORPUS.json OUTDIR [--version V | ORIGINAL CANDIDATE]\n"
            "       %(prog)s corpus PROJECT MW_VERSION OUT.json",
            description=__doc__,
            formatter_class=argparse.RawDescriptionHelpFormatter,
        )
        parser.add_argument("corpus", metavar="CORPUS.json")
        parser.add_argument("out", metavar="OUTDIR")
        parser.add_argument("compilers", nargs="*", metavar="ORIGINAL CANDIDATE", help="two compilers to compare")
        parser.add_argument("--version", default=project.PRIMARY, help=f"default {project.PRIMARY}")
        parser.add_argument(
            "--extab-padding",
            default="0000",
            metavar="HEX|raw",
            help="the two bytes both sides' exception-action padding is set to before comparing (default 0000), or "
            "raw to compare the objects as written",
        )
        options = parser.parse_args(args)
        try:
            padding = None if options.extab_padding == "raw" else extab.parse_padding(options.extab_padding)
        except extab.ExtabError as e:
            parser.error(f"--extab-padding: {e}")
        if options.compilers:
            if len(options.compilers) != 2:
                parser.error("give both ORIGINAL and CANDIDATE, or neither")
            original, candidate = (Path(c).resolve() for c in options.compilers)
        else:
            v = project.version(options.version, original=True)
            original, candidate = config.version_config(v).original, clang.output_path(v)
            result = subprocess.run(
                ["ninja", project.relative(candidate)], cwd=project.ROOT, capture_output=True, text=True
            )
            if result.returncode:
                raise project.Error(f"ninja {project.relative(candidate)} failed:\n{result.stdout[-3000:]}")
        for compiler in (original, candidate):
            if not Path(compiler).is_file():
                raise project.Error(f"{compiler} does not exist")
        if project.digest(Path(original).read_bytes()) == project.digest(Path(candidate).read_bytes()):
            # (the original against itself tests nothing)
            raise project.Error(f"{candidate} is the original compiler itself")
        if not run(options.corpus, Path(options.out).resolve(), original, candidate, padding):
            sys.exit(1)
    except (project.Error, extab.ExtabError) as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
