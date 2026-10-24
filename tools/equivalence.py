#!/usr/bin/env python3

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


def windows_program(path) -> bool:
    with open(path, "rb") as f:
        return f.read(2) == b"MZ"


def compile_one(root, compiler, job, output, native_sjis=False):
    windows = windows_program(compiler)
    command = build.windows_prefix() if windows else []
    environment = dict(os.environ, MWCIncludes=".")
    environment.pop("MWCC_SJIS", None)
    if job.get("sjis") and (native_sjis or not windows):
        environment["MWCC_SJIS"] = "1"
    elif job.get("sjis"):
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
            env=environment,
        )
    except subprocess.TimeoutExpired:
        return "timeout"
    if result.returncode or not output.exists():
        return (result.stdout + result.stderr)[-600:] or f"exit status {result.returncode}"
    return None


def same(a: bytes, b: bytes, padding: bytes | None) -> tuple[bool, int]:
    if a == b or padding is None:
        return a == b, 0
    (a, changed_a), (b, changed_b) = extab.normalize(a, padding), extab.normalize(b, padding)
    return a == b, changed_a + changed_b


def run(corpus_path, out, original, candidate, padding=b"\0\0", native_sjis=False):
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
        failure = compile_one(corpus["root"], candidate, job, b, native_sjis)
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
    root = Path(root).resolve()
    try:
        text = (root / "build.ninja").read_text().replace("$\n", "")
    except OSError as e:
        raise project.Error(f"{root}/build.ninja: {e.strerror} (configure the project first)") from None
    jobs, edge = [], None

    def flush():
        if edge and edge["vars"].get("mw_version") == mw_version:
            job = dict(source=edge["source"], cflags=shlex.split(edge["vars"]["cflags"]), sjis="sjis" in edge["rule"])
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
            description="Compile a corpus with the original compiler and a candidate, and compare the objects byte for byte.",
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
        parser.add_argument(
            "--native-sjis",
            action="store_true",
            help="run the candidate's sjis jobs with MWCC_SJIS=1 instead of through sjiswrap (a clang build's own "
            "conversion; a Linux build always does)",
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
            raise project.Error(f"{candidate} is the original compiler itself")
        if not run(options.corpus, Path(options.out).resolve(), original, candidate, padding, options.native_sjis):
            sys.exit(1)
    except (project.Error, extab.ExtabError) as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
