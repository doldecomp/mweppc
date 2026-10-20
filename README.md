# Metrowerks CodeWarrior

A decompilation of the Metrowerks CodeWarrior toolchain. Currently supported versions:

| Compiler       | Version    | Internal version | Exact functions | Linked functions    | Linked bytes | Notes                         |
| -------------- | ---------- | ---------------- | --------------- | ------------------- | ------------ | ----------------------------- |
| GameCube 2.0   | `GC_2_0`   | 2.4.7 build 92   | 4016 / 4065     | 2850 / 4068 (70.1%) | 54.3%        |                               |
| GameCube 2.0p2 | `GC_2_0p2` | 2.4.7 build 92   | 4019 / 4068     | 2853 / 4071 (70.1%) | 54.3%        | GameCube 2.0 patched for BFBB |
| GameCube 2.5   | `GC_2_5`   | 2.4.7 build 105  | 4026 / 4076     | 2860 / 4079 (70.1%) | 54.3%        |                               |
| GameCube 2.6   | `GC_2_6`   | 2.4.7 build 107  | 4027 / 4077     | 2860 / 4080 (70.1%) | 54.4%        |                               |
| GameCube 2.7   | `GC_2_7`   | 2.4.7 build 108  | 4033 / 4084     | 2848 / 4087 (69.7%) | 54.2%        |                               |

- **Exact:** the compiled function has identical assembly to the original binary.
- **Linked:** the functions whose translation unit links identically to the original executable.

- Every function that isn't currently exact has been proven equivalent (see: `tools/prove.py`), compiles from source
  alone, and has been checked against a number of decompilations to make sure it produces identical assembly to the
  original binary.
- Currently only the C/C++ compiler has been decompiled (`mwcceppc.exe`). See [future work](#future-work) for more
  information.
- 2.0p2 is not an official binary; it models the observed side effects of compiling the [*Battle for Bikini
  Bottom*](https://github.com/bfbbdecomp/bfbb) decompilation against GameCube 2.0 since it seems to have been compiled
  with a unique patched compiler. See [below](#battle-for-bikini-bottom-20p2) for more information.

## Getting started

### Dependencies

- Linux (x86-64, or arm64 for the clang build alone), macOS or Windows.
- Python 3.11 or newer, with [uv](https://docs.astral.sh/uv/) installed.
- The [ninja](https://ninja-build.org) build system.
- [LLVM](https://llvm.org) 16 or newer: `clang`, `lld-link`, `llvm-rc` and `llvm-dlltool` (Debian and Ubuntu:
  `clang`, `lld` and `llvm`).

Everything else (the original compilers, library code used straight from CodeWarrior discs, and various tooling) are
downloaded automatically and verified against a pinned SHA-256.

### Building

```sh
uv sync
source .venv/bin/activate
python configure.py
ninja
```

By default, this builds every supported compiler with clang under `build/VERSION/clang/mwcceppc.exe`, as well as
`compile_commands.json` for LSP support. This should be a drop-in replacement to the original compiler and can be used
the same as before.

`configure.py --version GC_2_6` configures only the specified versions to be built using clang.

`configure.py --mwcc` sets up the original matching workflow instead, and `ninja` then builds every supported compiler
and verifies it matches the original binary. It builds `build/VERSION/source-only/mwcceppc.exe` from just the source
code, which may be equivalent but may be non-matching.

`configure.py --matching` (implies `--mwcc`) builds `build/VERSION/mwcceppc.exe` using a combination of matched/linked
TUs and original objects; 1:1 to the original compiler. It also builds `mwcceppc-dwarf.exe` which has DWARF 4 metadata
embedded, plus `mwcceppc-debug.exe` + `mwcceppc-debug.pdb` which contains debug information, either of which can be used
in IDA Pro, Ghidra, or Binary Ninja.

### Tooling

Every tool in `tools/` runs with the venv's `python` and explains itself with `--help`.

- `status.py VERSION --todo`: the units nearest to linking, and what blocks each.
- `fn.py`: a function's original code, its diff against the compiled code, and alternative definitions tried in
  parallel.
- `unit.py`, `headers.py`, `includes.py`: move functions, declarations and includes to where they belong.
- `rename.py`: rename anything everywhere: code, configuration and symbols.
- `evidence.py`: what each function is called, from the original's asserts and the Wii compilers' symbols.
- `verify.py` and `regressions.py`: check every version, and what stopped matching since your saved baseline.
- `layout.py`, `relink.py`: lay out the original's symbols, and see which sources link into it.
- `prove.py`: prove an inexact function equivalent to the original's.
- `equivalence.py`: compile a decompilation project's sources with two compilers and compare the objects.
- `clang_check.py`, `truefalse.py`, `update_assert_lines.py`: source checks and fix-ups.
- `bindings.py`, `validate.py`, `compilers.py`, `crosscheck.py`, `port_version.py`, `gen_options.py`: the configuration,
  and how the original was built.

## Accuracy

### The clang build

The majority of the source code matches the original compiler, which defines what bugs exist when compiling code. All
support needed to build equivalent objects with a modern clang compiler has been wrapped inside the `#else` version of
`#ifdef __MWERKS__`. Some of the quirks that had to be accounted for (includes compiler flags) include:

- **Floating point.** The original computes on the x87 at 64-bit precision; `include/x87.h` emulates it in integer
  arithmetic, so results never depend on the host.
- **Undefined behaviour.** Signed and pointer arithmetic wrap, aliasing and null checks are not assumed, and locals read
  before being set read as zero.
- **The original's own memory.** Where the original's output depends on its addresses, on data past the end of its
  tables or on stale stack contents (the bugs below), the clang build reproduces the original's values.

### Known bugs

These are some of the known bugs of the original Metrowerks compilers (clang reproduces these as best it can):

- **Code generation**
  - A `bool` conversion is dropped when a pointer's target type sits at an address ending in `00` (`short double*` in
    2.5 to 2.7).
  - A load through a pointer to const is reused across stores through other pointers, and in 2.5 and later hoisted out
    of loops: `while (*p > 0) { (*q)--; }` never ends when `p == q`.
  - Peephole's `L_S` decides whether to delete a store from two stack words it never set.
  - The 7450, 601 and 821 scheduling models read past the end of their tables.
  - A float literal with a decimal exponent past about ±384 reads past MSL's table: `1e401` is 0.
  - A source file name of 64 or 65 characters overruns its buffer, and can change the code.
- **Hangs and crashes**
  - `#pragma scheduling 821` with two FP-unit instructions, `mflr` among them, never finishes.
  - Very long `-o` or `precompile_target` names, and `struct ::E` inside a class template, crash the compiler.
- **Not reproducible**, because they vary with the host:
  - two padding bytes in some C++ exception tables (decomp-toolkit's `extab_padding` sets them);
  - a stack address in a local class's mangled name;
  - the bytes of precompiled headers;
  - the x87's starting precision on Windows (53 bits, against 64 under wibo and in the clang build).
- **Where the clang build differs:** it keeps going where the original crashes, and does not follow a `#pragma
  scheduling` that switches models within one file.

### Battle for Bikini Bottom (2.0p2)

*Battle for Bikini Bottom*'s retail code was built with a 2.0 that no release matches. Its decompilations have used
binary patches of 2.0 (`2.0p1`, and bfbb-vibe's `2.0p1g`). 2.0p2 is the same idea done using the decompiled source code:
2.0 with rules of its own, each one measured on BFBB's code. Functions matching retail:

| Compiler  | bfbb (of 7673) | bfbb-vibe (of 7673) | *Ratatouille*'s shared code (of 321) |
| --------- | -------------- | ------------------- | ------------------------------------ |
| 2.0p1     | 5474           | 6994                | 208                                  |
| 2.0p1g    | 5724           | 7579                | 217                                  |
| **2.0p2** | **5731**       | **7594**            | 217                                  |

A GameCube prototype of [*Ratatouille*](https://github.com/seilc/rat) using the same engine was used as an independent
check to deduce the required behavior by compiling it using BFBB's settings.

Here is everything that got changed from 2.0, all aimed at globals addressed directly in small data (`sym@sda21`), which
BFBB's developers seemingly required a custom patch to the compiler for to treat more conservatively:

- floating-point literals are not const, and stores may change them;
- a store to a small-data global may change any memory a pointer reaches;
- the scheduler orders loads from small data like loads through a pointer;
- a store to a global array's element forgets the small-data globals;
- a load of a small-data global stays inside its loop;
- two of 2.5's alias analysis changes.

The "stays inside its loop" rule also covers const globals, and only one function needs that: `zParPTankSpawnSnow`,
whose `snow_life` is declared `const` in the bfbb decompilation but sits in writable data in the retail game, and so is
probably not `const` at all. With that fixed, the rule may not need to cover const globals; that is for BFBB decomp to
figure out.

To use 2.0p2 in a BFBB decompilation, put `build/GC_2_0p2/clang/mwcceppc.exe` in place of the project's 2.0p1
compiler.

Comparing against the retail game also shows a few places where bfbb's source differs from what the original must have
been:

- `x--; if (x == 0)` should be `if (--x == 0)` in `xSGShutdown` and ten other functions;
- `snow_life` should not be `const`;
- `NPAR_TubeSpiralMagic`'s colours should be `static const` locals.

## Contributing

- Format C with `clang-format -i`, and Python with `ruff format` and `ruff check` (the venv's pinned versions); CI
  checks both and builds every version with clang.
- Braces on every `if`, `else`, `for`, `while` and `do` body; flags are `TRUE` and `FALSE`.
- After editing a header, run `tools/verify.py`; MWCC's code can change with unrelated declarations.
- Put clang-only code under `#ifdef __MWERKS__ ... #else ... #endif`.
- Rerun `tools/prove.py` on any inexact function you change.

## Future work

In scope, with no promise of when:

- the other GameCube compilers: 1.0 to 1.3.2 (decompiled separately, to be rewritten and merged) and the 3.0 alphas;
- the Wii compilers, whose public symbols give real names from the start;
- the linker and assembler;
- CodeWarrior's other platforms, such as Nintendo DS and PlayStation 2.

## Credits

- Ninji's decompilation of the CodeWarrior Pro 7 Mac OS X compiler (front end 2.4.5) was used as the basis for the
  majority of the code here, including names and structure, reworked for what the 2.X compilers actually do.
- [rayanht/mwcc](https://github.com/rayanht/mwcc) (GC 1.X) and
  [henriquegemignani/mwcc](https://github.com/henriquegemignani/mwcc) (`build-1.3`) were used as a reference for what
  earlier GameCube compilers were doing and shaped the overall repo.
- [decomp.dev](https://decomp.dev)'s compiler archive, [wibo](https://github.com/decompals/wibo),
  [objdiff](https://github.com/encounter/objdiff), [UnshieldSharp](https://github.com/mnadareski/UnshieldSharp) and the
  Internet Archive's CodeWarrior collection.

## License

This decompilation is dedicated to the public domain (CC0, `LICENSE`).
