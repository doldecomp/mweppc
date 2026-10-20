from __future__ import annotations

import contextlib
import hashlib
import os
import re
import sys
import tempfile
import threading
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

PRIMARY = "GC_2_6"


class Error(Exception):
    pass


@dataclass(frozen=True)
class Version:
    name: str
    index: int
    base: str | None = None

    @property
    def config(self) -> Path:
        return ROOT / "config" / self.name

    @property
    def build(self) -> Path:
        return ROOT / "build" / self.name

    def __str__(self) -> str:
        return self.name


def _read_versions() -> tuple[Version, ...]:
    header = (ROOT / "include/version.h").read_text()
    directories = {p.name.upper(): p.name for p in (ROOT / "config").iterdir() if (p / "config.json").exists()}
    versions = []
    for name, index in re.findall(r"^#define VERSION_(\w+) (\d+)$", header, re.MULTILINE):
        if name.upper() not in directories:
            raise Error(f"include/version.h defines VERSION_{name}, but config/ has no directory for it")
        raw = (ROOT / "config" / directories[name.upper()] / "config.json").read_text()
        base = re.search(r'"derived_from": "(\w+)"', raw)
        versions.append(Version(directories[name.upper()], int(index), base[1] if base else None))
    if sorted(v.index for v in versions) != list(range(len(versions))):
        raise Error("include/version.h: the VERSION_* indices are not 0, 1, 2, ...")
    extra = set(directories.values()) - {v.name for v in versions}
    if extra:
        raise Error(f"config/ has {', '.join(sorted(extra))}, which include/version.h does not define")
    return tuple(sorted(versions, key=lambda v: v.index))


ALL_VERSIONS = _read_versions()
VERSIONS = tuple(v for v in ALL_VERSIONS if v.base is None)
DERIVED = tuple(v for v in ALL_VERSIONS if v.base is not None)


def version(name: str | Version | None = None, *, original: bool = False) -> Version:
    found = name if isinstance(name, Version) else next((v for v in ALL_VERSIONS if v.name == (name or PRIMARY)), None)
    if found is None:
        raise Error(f"unknown version {name!r} (one of {', '.join(v.name for v in ALL_VERSIONS)})")
    if original and found.base:
        raise Error(
            f"{found} is derived from {found.base} and has no original executable: only its compiler from the sources "
            f"is built (ninja build/{found}/source-only/mwcceppc.exe)"
        )
    return found


def relative(path: Path | str) -> str:
    path = Path(path)
    if path.is_absolute():
        lexical = Path(os.path.normpath(path))
        if lexical.is_relative_to(ROOT):
            path = lexical.relative_to(ROOT)
        else:
            resolved = path.resolve()
            path = resolved.relative_to(ROOT) if resolved.is_relative_to(ROOT) else resolved
    return path.as_posix()


def digest(data: bytes) -> str:
    return hashlib.sha1(data).hexdigest()


def _umask() -> int:
    mask = os.umask(0)
    os.umask(mask)
    return mask


_NEW_FILE_MODE = 0o666 & ~_umask()


def write_atomic(path: Path | str, data: bytes | str) -> None:
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    if isinstance(data, str):
        data = data.encode()
    try:
        mode = path.stat().st_mode & 0o7777
    except FileNotFoundError:
        mode = _NEW_FILE_MODE
    fd, temporary = tempfile.mkstemp(prefix=f".{path.name}.", suffix=".tmp", dir=path.parent)
    try:
        with os.fdopen(fd, "wb") as f:
            f.write(data)
        os.chmod(temporary, mode)
        os.replace(temporary, path)
    except BaseException:
        with contextlib.suppress(FileNotFoundError):
            os.unlink(temporary)
        raise


_LOCK_PATH = ROOT / "build" / ".config.lock"
_lock_state = threading.local()


@contextlib.contextmanager
def locked():
    depth = getattr(_lock_state, "depth", 0)
    if depth:
        _lock_state.depth += 1
        try:
            yield
        finally:
            _lock_state.depth -= 1
        return
    _LOCK_PATH.parent.mkdir(parents=True, exist_ok=True)
    with open(_LOCK_PATH, "a+b") as f:
        if sys.platform == "win32":
            import msvcrt

            f.seek(0)
            while True:
                try:
                    msvcrt.locking(f.fileno(), msvcrt.LK_LOCK, 1)
                    break
                except OSError:
                    continue
        else:
            import fcntl

            fcntl.flock(f.fileno(), fcntl.LOCK_EX)
        _lock_state.depth = 1
        try:
            yield
        finally:
            _lock_state.depth = 0
            if sys.platform == "win32":
                import msvcrt

                f.seek(0)
                msvcrt.locking(f.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                import fcntl

                fcntl.flock(f.fileno(), fcntl.LOCK_UN)


class StaleFile(Error):
    pass


class Transaction:
    def __init__(self):
        self.read_digests: dict[Path, str | None] = {}
        self.staged: dict[Path, bytes | None] = {}

    def _path(self, path) -> Path:
        path = Path(path)
        return path if path.is_absolute() else ROOT / path

    def read_bytes(self, path) -> bytes | None:
        path = self._path(path)
        if path in self.staged:
            return self.staged[path]
        try:
            data = path.read_bytes()
        except FileNotFoundError:
            data = None
        self.read_digests.setdefault(path, None if data is None else digest(data))
        return data

    def read(self, path) -> str:
        data = self.read_bytes(path)
        if data is None:
            raise Error(f"{relative(self._path(path))} does not exist")
        return data.decode()

    def unchanged(self, path, text: str) -> None:
        data = self.read_bytes(path)
        if data is None or data.decode("latin-1") != text:
            raise StaleFile(f"{relative(self._path(path))} changed while this ran: nothing written")

    def write(self, path, data: bytes | str | None) -> None:
        path = self._path(path)
        if path not in self.read_digests:
            self.read_bytes(path)
        self.staged[path] = data.encode() if isinstance(data, str) else data

    def changes(self) -> dict[Path, bytes | None]:
        changed = {}
        for path, data in self.staged.items():
            try:
                old = path.read_bytes()
            except FileNotFoundError:
                old = None
            if old != data:
                changed[path] = data
        return changed

    def commit(self) -> list[Path]:
        from concurrent.futures import ThreadPoolExecutor

        from . import clang

        sources = [p for p, data in self.staged.items() if data is not None and clang.formatted(p)]
        with ThreadPoolExecutor() as pool:
            for path, data in zip(sources, pool.map(lambda p: clang.format_source(p, self.staged[p]), sources)):
                self.staged[path] = data
        with locked():
            for path, expected in self.read_digests.items():
                try:
                    now = digest(path.read_bytes())
                except FileNotFoundError:
                    now = None
                if now != expected:
                    raise StaleFile(f"{relative(path)} changed while this tool was working: run it again")
            changed = self.changes()
            for path, data in changed.items():
                if data is None:
                    path.unlink()
                else:
                    write_atomic(path, data)
        self.staged.clear()
        return sorted(changed)

    def __enter__(self):
        return self

    def __exit__(self, kind, value, traceback):
        if kind is None:
            self.commit()
        return False
