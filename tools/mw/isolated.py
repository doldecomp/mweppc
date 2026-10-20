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
FORK = "fork" in multiprocessing.get_all_start_methods()


@dataclass
class Result:
    ok: bool
    object: Path | None
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
        directory = Path(tempfile.mkdtemp(dir=self.path))
        source = directory / Path(compilation.source).name
        if text is None:
            shutil.copyfile(ROOT / compilation.source, source)
        else:
            source.write_bytes(text.encode("latin-1") if isinstance(text, str) else text)
        output = directory / "out.obj"
        args = compilation.argv(relative(output), relative(source))
        include = f"-I{Path(compilation.source).parent.as_posix()}"
        args = (
            [*args[:-3], include, *args[-3:]] if not compilation.pro4 else [*args[:-2], "/" + include[1:], *args[-2:]]
        )
        try:
            result = build.run([*build.windows_prefix(), compilation.executable, *args], timeout=timeout)
        except Exception as e:
            return Result(False, None, f"{type(e).__name__}: {e}")
        log = result.stdout + result.stderr
        ok = result.returncode == 0 and output.exists()
        return Result(ok, output if ok else None, log)


_tasks: dict[int, tuple] = {}
_in_worker = False


def _worker_start() -> None:
    global _in_worker
    _in_worker = True


def _call(token: int, index: int):
    function, items = _tasks[token]
    return function(items[index])


def parallel(function, items, jobs: int | None = None):
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
    built = build.object_path(compilation.version, compilation.source)
    if not built.exists():
        return False, f"{relative(built)} does not exist: build first"
    result = workspace.compile(compilation)
    if not result.ok:
        return False, "the unchanged source does not compile: " + result.log[-500:]
    return (True, "") if same_object(result.object, built) else (False, "the unchanged source compiles differently")
