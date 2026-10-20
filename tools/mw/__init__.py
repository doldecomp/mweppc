"""The repository's tooling library.

Every script in tools/ is a thin command over these modules, so each file format, path, compile command and verdict
has one implementation:

- project: the repository root, versions, paths, atomic writes, the repository lock and transactions
- config:  the hand-written configuration (sources.json, config/VERSION/*.json), loaded and validated
- formats: symbols.txt and splits.txt
- coff, pe: object files, the original executable
- extab:   the C++ exception tables in the PowerPC objects the compiler writes, and their padding
- names:   a function's or variable's spellings as the linker sees them
- build:   how a source is compiled (the one place configure.py and every other tool get a command line from)
- isolated: compiling variants of a source outside the build
- verdict: whether a compiled function is the original's (the one definition of exact)
- proof:   whether an inexact function computes what the original's does (translation validation)
- link:    the original's linker: the relink's inputs and command line, import libraries
- renaming: renaming functions, variables and units everywhere the repository names them
- asserts: the original's assert sites (file and line)
- wii:     the Wii compilers' CodeView (every function's module, the public names)
- pairing: pairing two builds' functions by their code
- cindex:  a tolerant index of the function definitions in a C tree (reference decompilations)
- clang:   the one clang command line for a source (compile_commands.json, diagnostics, syntax trees), and the
           compiler built with clang (its flags, its link)
- csrc:    the sources as the editing tools see them: definitions from clang, with their comments, pragmas and #if
- codeview, pdb, dwarf: the compiler's CodeView read, and PDB and DWARF written (tools/link_debug.py)
"""
