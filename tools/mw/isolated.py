"""Compiling variants of a source without touching the build.

A Workspace is a private directory under build/tmp/ (removed when the workspace closes, Ctrl-C included). Each variant
is written there under the source's own file name, so __FILE__ and the compiler's literals are the real build's, and
compiled with the source's real command line (build.compilation) plus -I of the source's directory, so its
"local" includes still resolve. Nothing in src/ or build/VERSION/compiled is written, so any number of tools can try
variants at once, beside ninja.

reproduces() checks the method for one source: the unchanged text compiled so must give ninja's object (every section
but the uninitialized ones, whose file bytes the compiler leaves as junk, and the debug and directive sections).
"""

from __future__ import annotations

import concurrent.futures
import multiprocessing
import os
import shutil
import tempfile
from dataclasses import dataclass
from pathlib import Path

from . import build
from .coff import Object
from .project import ROOT, relative

TMP = ROOT / "build" / "tmp"
# (whether worker processes can be forked, inheriting the caller's memory)
FORK = "fork" in multiprocessing.get_all_start_methods()


@dataclass
class Result:
    ok: bool
    object: Path | None  # the compiled object (inside the workspace: read it before the workspace closes)
    log: str


class Workspace:
    def __init__(self, keep: bool = False):
        TMP.mkdir(parents=True, exist_ok=True)
        self.path = Path(tempfile.mkdtemp(prefix=f"{os.getpid()}-", dir=TMP))
        self.keep = keep

    def close(self) -> None:
        if not self.keep:
            shutil.rmtree(self.path, ignore_errors=True)

    def __enter__(self) -> Workspace:
        return self

    def __exit__(self, *_) -> None:
        self.close()

    def compile(self, compilation: build.Compilation, text: str | bytes | None = None, timeout: float = 600) -> Result:
        """Compile TEXT (default: the source as it is) as COMPILATION's source."""
        # (a directory of its own: compiles may run in forked processes, which share no counter)
        directory = Path(tempfile.mkdtemp(dir=self.path))
        source = directory / Path(compilation.source).name
        if text is None:
            shutil.copyfile(ROOT / compilation.source, source)
        else:
            source.write_bytes(text.encode("latin-1") if isinstance(text, str) else text)
        output = directory / "out.obj"
        args = compilation.argv(relative(output), relative(source))
        include = f"-I{Path(compilation.source).parent.as_posix()}"
        # (before the source and output, which the pro4 form needs last)
        args = (
            [*args[:-3], include, *args[-3:]] if not compilation.pro4 else [*args[:-2], "/" + include[1:], *args[-2:]]
        )
        try:
            result = build.run([*build.windows_prefix(), compilation.executable, *args], timeout=timeout)
        except Exception as e:  # (a timeout: report it like a failed compile)
            return Result(False, None, f"{type(e).__name__}: {e}")
        log = result.stdout + result.stderr
        ok = result.returncode == 0 and output.exists()
        return Result(ok, output if ok else None, log)


# (the running parallel() calls' functions and items: forked workers inherit them, so only an index crosses to a
# worker and FUNCTION may be a lambda or closure)
_tasks: dict[int, tuple] = {}
_in_worker = False


def _worker_start() -> None:
    global _in_worker
    _in_worker = True


def _call(token: int, index: int):
    function, items = _tasks[token]
    return function(items[index])


def parallel(function, items, jobs: int | None = None):
    """FUNCTION of each of ITEMS, in order: JOBS or $JOBS at once, else one per core. Each runs in a forked process,
    since judging a compiled object is Python work that threads would serialise on the GIL (fn.py's trials ran 4-5
    times slower in threads); FUNCTION's results must be picklable, and anything it changes in memory is lost (files
    it writes are not). A parallel() inside a worker, or of one item, runs in threads, and so does every one where
    processes cannot fork (Windows). Ctrl-C drops what has not started."""
    items = list(items)
    jobs = jobs or int(os.environ.get("JOBS") or 0) or os.cpu_count() or 4
    if _in_worker or len(items) < 2 or not FORK:
        pool = concurrent.futures.ThreadPoolExecutor(jobs)
        futures = [pool.submit(function, item) for item in items]
    else:
        token = id(items)
        _tasks[token] = (function, items)
        pool = concurrent.futures.ProcessPoolExecutor(
            min(jobs, len(items)), mp_context=multiprocessing.get_context("fork"), initializer=_worker_start
        )
        futures = [pool.submit(_call, token, i) for i in range(len(items))]
    try:
        for future in futures:
            yield future.result()
    finally:
        pool.shutdown(wait=True, cancel_futures=True)
        _tasks.pop(id(items), None)


def comparable(obj: Object) -> list:
    """An object's contents as far as code generation goes: each section but .bss, .drectve and the debug sections, with
    its relocations by symbol name."""
    out = []
    for s in obj.sections:
        if s.bss or s.name == ".drectve" or s.name.startswith(".debug"):
            continue
        out.append(
            (s.name, s.flags, s.data, sorted((r.offset, obj.symbol(r.symbol).name, r.kind) for r in s.relocations))
        )
    return out


def same_object(a: Path | Object, b: Path | Object) -> bool:
    a = a if isinstance(a, Object) else Object.read(a)
    b = b if isinstance(b, Object) else Object.read(b)
    return comparable(a) == comparable(b)


def reproduces(compilation: build.Compilation, workspace: Workspace) -> tuple[bool, str]:
    """Whether compiling the source in WORKSPACE gives the build's object (which must be current)."""
    built = build.object_path(compilation.version, compilation.source)
    if not built.exists():
        return False, f"{relative(built)} does not exist: build first"
    result = workspace.compile(compilation)
    if not result.ok:
        return False, "the unchanged source does not compile: " + result.log[-500:]
    return (True, "") if same_object(result.object, built) else (False, "the unchanged source compiles differently")
