from __future__ import annotations

import os
import shutil
import subprocess
import sys
from dataclasses import dataclass, replace
from pathlib import Path

from . import config
from .project import ROOT, Error, Version
from .project import version as get_version

WINDOWS = sys.platform == "win32"

INCLUDES_VARIABLE = ("MWCIncludes", "include/libc")


@dataclass(frozen=True)
class Compilation:
    version: Version
    source: str
    compiler: str
    args: tuple[str, ...]

    @property
    def executable(self) -> str:
        return f"build/compilers/{self.compiler}/mwcc.exe"

    @property
    def pro4(self) -> bool:
        return self.compiler == "pro4"

    @property
    def flags(self) -> tuple[str, ...]:
        return tuple(a for a in self.args[1:] if not a.startswith(("-D", "-I")) and a != "-i-")

    def with_symbols(self) -> Compilation:
        args = list(self.args)
        if "-sym" in args:
            args[args.index("-sym") + 1] = "on"
        else:
            args[1:1] = ["-sym", "on"]
        return replace(self, args=tuple(args))

    def argv(self, output: str, source: str | None = None, depfile: bool = False) -> list[str]:
        source = source or self.source
        if self.pro4:
            return pro4_arguments([*self.args, source, "-o", output])
        return [*self.args, *(["-MD"] if depfile else []), source, "-o", output]

    def command(self, output: str, source: str | None = None, depfile: bool = False) -> list[str]:
        return [*windows_prefix(), self.executable, *self.argv(output, source, depfile)]


def pro4_arguments(args: list[str]) -> list[str]:
    out, rest = [], iter(args)
    for arg in rest:
        if arg == "-o":
            out.append("/Fo" + next(rest))
        elif arg.startswith("-"):
            out.append("/" + arg[1:])
        else:
            out.append(arg)
    return out


def host_compiler(cfg: config.VersionConfig, source: config.Source) -> str:
    return cfg.compiler_overrides.get(source.path) or cfg.compilers.get(source.compiler, source.compiler)


def compilation(version: Version | str | None, source: str, *, _sources=None, _config=None) -> Compilation:
    v = get_version(version)
    table = _sources if _sources is not None else config.sources()
    if source not in table:
        raise Error(f"{source} is not in config/sources.json")
    cfg = _config if _config is not None else config.version_config(v)
    settings = table[source]
    flags = list(cfg.flags.get(source, settings.flags))
    args = [
        "-c",
        *flags,
        f"-DVERSION={v.index}",
        *(f"-I{d}" for d in settings.include_dirs),
        "-Iinclude",
        "-i-",
        *(f"-I{d}" for d in settings.system_include_dirs),
        "-Iinclude/libc",
    ]
    return Compilation(v, source, host_compiler(cfg, settings), tuple(args))


def compilations(version: Version | str | None = None) -> list[Compilation]:
    v = get_version(version)
    table, cfg = config.sources(), config.version_config(v)
    return [compilation(v, s, _sources=table, _config=cfg) for s in table]


def object_path(version: Version | str | None, source: str, debug: bool = False) -> Path:
    v = get_version(version)
    return v.build / ("debug/compiled" if debug else "compiled") / f"{source}.obj"


def wibo() -> str:
    path = os.environ.get("WIBO") or str(ROOT / "build/tools/wibo")
    if not (Path(path).exists() or shutil.which(path)):
        raise Error(f"{path} does not exist: run ninja once (it downloads wibo), or set WIBO")
    return path


def windows_prefix() -> list[str]:
    return [] if WINDOWS else [wibo()]


def environment(**extra) -> dict[str, str]:
    env = dict(os.environ)
    env[INCLUDES_VARIABLE[0]] = INCLUDES_VARIABLE[1]
    env.update(extra)
    return env


def run(
    command: list[str], cwd: Path | str = ROOT, timeout: float | None = 600, env=None, check: bool = False
) -> subprocess.CompletedProcess:
    if WINDOWS and command and not Path(command[0]).is_absolute() and (Path(cwd) / command[0]).exists():
        # (Windows looks a relative program up from this process's directory, not cwd's)
        command = [str(Path(cwd) / command[0]), *command[1:]]
    result = subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=timeout, env=env or environment()
    )
    if check and result.returncode:
        raise Error(
            f"{' '.join(command[:3])} ... failed ({result.returncode}):\n{(result.stdout + result.stderr)[-3000:]}"
        )
    return result
