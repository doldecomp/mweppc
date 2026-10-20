"""How each source is compiled, and running the Windows tools.

compilation(version, source) is the one definition of a source's command line: configure.py writes build.ninja from
it, and every tool that compiles outside ninja (tools/isolated.py's users) uses it too, so they cannot drift apart.

A source's settings come from config/sources.json, changed per version by config/VERSION/config.json:

    compilers            {sources.json's compiler: this version's}, e.g. pro53 -> pro6 for every 2.x version
    compiler_overrides   {source: compiler}: built with that compiler, whatever the mapping says
    flags                {source: [flags]}: replaces sources.json's flags

The command runs from the repository root, with relative paths: the compiler records the paths it is given, and even
an include directory's name has been seen to change a function's code.
"""

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

# (the CodeWarrior compilers find the C library's headers through this variable as well as -I)
INCLUDES_VARIABLE = ("MWCIncludes", "include/libc")


@dataclass(frozen=True)
class Compilation:
    version: Version
    source: str
    compiler: str  # the host compiler: pro4, pro5, pro53 or pro6
    args: tuple[str, ...]  # everything but the source and output: -c, flags, defines, includes

    @property
    def executable(self) -> str:
        return f"build/compilers/{self.compiler}/mwcc.exe"

    @property
    def pro4(self) -> bool:
        return self.compiler == "pro4"

    @property
    def flags(self) -> tuple[str, ...]:
        """The compiler flags alone (without -c, -D and -I)."""
        return tuple(a for a in self.args[1:] if not a.startswith(("-D", "-I")) and a != "-i-")

    def with_symbols(self) -> Compilation:
        """The same compile with symbols on (CodeView, for the debug executables)."""
        args = list(self.args)
        if "-sym" in args:
            args[args.index("-sym") + 1] = "on"
        else:
            args[1:1] = ["-sym", "on"]
        return replace(self, args=tuple(args))

    def argv(self, output: str, source: str | None = None, depfile: bool = False) -> list[str]:
        """The compiler's arguments to build OUTPUT (from SOURCE, by default the real one); without the program."""
        source = source or self.source
        if self.pro4:
            return pro4_arguments([*self.args, source, "-o", output])
        return [*self.args, *(["-MD"] if depfile else []), source, "-o", output]

    def command(self, output: str, source: str | None = None, depfile: bool = False) -> list[str]:
        """The whole command line (through wibo off Windows)."""
        return [*windows_prefix(), self.executable, *self.argv(output, source, depfile)]


def pro4_arguments(args: list[str]) -> list[str]:
    """Pro 4's driver takes Microsoft-style options (/c, /Fo...)."""
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
    """How VERSION builds SOURCE (a path relative to the root, as config/sources.json names it)."""
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
    """Every source's compilation, in config/sources.json's order."""
    v = get_version(version)
    table, cfg = config.sources(), config.version_config(v)
    return [compilation(v, s, _sources=table, _config=cfg) for s in table]


def object_path(version: Version | str | None, source: str, debug: bool = False) -> Path:
    v = get_version(version)
    return v.build / ("debug/compiled" if debug else "compiled") / f"{source}.obj"


# ------------------------------------------------------------------------------------------- running Windows tools


def wibo() -> str:
    """The Win32 loader that runs the compilers off Windows: $WIBO, else build/tools/wibo (which ninja downloads)."""
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
    """Run a command (a Windows tool through windows_prefix()) with the compilers' environment."""
    result = subprocess.run(
        command, cwd=cwd, capture_output=True, text=True, errors="replace", timeout=timeout, env=env or environment()
    )
    if check and result.returncode:
        raise Error(
            f"{' '.join(command[:3])} ... failed ({result.returncode}):\n{(result.stdout + result.stderr)[-3000:]}"
        )
    return result
