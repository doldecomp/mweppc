#!/usr/bin/env python3
"""Prove inexact functions equivalent to the original's.

Each function (by default every one the last comparison found inexact: build/VERSION/compare.json, refused when out of
date: run ninja) is taken from the build's object, its references resolved as the comparison resolves them, and
executed symbolically beside the original's code (tools/mw/proof.py says how, and what the model assumes). PROVED
means the two compute the same results, memory and calls, path by path; otherwise the reason names the first place
they disagree, or what the model does not cover (a disagreement the model cannot settle is never a proof). Statics the
comparison cannot place are placed where the original's aligned code refers, and listed. The results are written to
build/VERSION/proofs.json. --trace writes each pair of program points the proof runs (with one NAME) to FILE.

--text FILE (with one NAME): prove NAME compiled from FILE (its whole source, edited) outside the build instead, to
try a change to the source. Where only the types show two accesses apart, DISJOINT below names them, with why.

--mutate [N]: test the prover itself, after a change to tools/mw/proof.py: N (default 40) mutants of each function
build/VERSION/proofs.json has proved (one bit of one instruction flipped, its length kept), each proved as the
function was. A mutant that still proves is a hole in the model unless it computes the same (dead code, an
instruction with the same effect there), which only reading it tells; the October 2026 runs found five holes this way.
2.6's 2000 take about 40 minutes with JOBS=12; run nothing else heavy beside it.

Exits 1 unless every function is proved.
"""

import argparse
import contextlib
import json
import random
import re
import resource
import sys
from pathlib import Path

import capstone

sys.path.insert(0, str(Path(__file__).parent))
from mw import build, clang, isolated, project, proof, verdict

# (pairs of instructions in a function's compiled code, by offset from its start, whose memory accesses never overlap,
# with why: the model has no types, so where only they show it, the proof takes it as an assumption it names. A pair
# that no longer names memory accesses fails the proof; one the proof does not use is reported)
DISJOINT = {
    "GeneratePossibleCondAss": (
        (0x50D, 0x514),
        "`fnode->last->type = IROLinearNop` stores offset 0 of an IROLinear, `value1 = ass1->u.diadic.right` reads "
        "offset 0x24 of an IROLinear (the original reads it first): two IROLinear objects are one object, whose fields "
        "at 0 and 0x24 are apart, or two, which do not overlap",
    ),
}

# (the functions that never return: each ends in longjmp)
NORETURN = (
    "CError_ErrorTerm",
    "CError_Internal",
    "CError_ExpressionTooComplex",
    "CError_NoMem",
    "CError_UserBreak",
    "CError_CannotOpen",
    "longjmp",
    "exit",
    "abort",
)


class Types:
    """Sizes of C types as MWCC lays them out, from clang's syntax tree of a source: scalars, enums (the smallest
    integral type that holds their values, MWCC's rule unless enumsalwaysint is on), arrays and structs (fields at
    their natural alignment)."""

    _SCALARS = (
        (re.compile(r"\b(char|_Bool)$"), 1),
        (re.compile(r"\bshort( int)?$"), 2),
        (re.compile(r"\blong long( int)?$"), 8),
        (re.compile(r"\bdouble$"), 8),
        (re.compile(r"\b(int|long|float|long int)$"), 4),
    )

    def __init__(self, tree: dict):
        self.typedefs, self.enums, self.records = {}, {}, {}
        for n in tree.get("inner", []):
            kind = n.get("kind")
            if kind == "TypedefDecl":
                self.typedefs[n["name"]] = n["type"]["qualType"]
            elif kind == "EnumDecl" and n.get("name"):
                self.enums[n["name"]] = self._enum(n)
            elif kind == "RecordDecl" and n.get("name") and n.get("completeDefinition"):
                self.records[f"{n['tagUsed']} {n['name']}"] = n

    @staticmethod
    def _enum(n: dict) -> int:
        values, last = [], -1
        for c in n.get("inner", []):
            if c.get("kind") != "EnumConstantDecl":
                continue
            given = [x["value"] for x in c.get("inner", []) if "value" in x]
            last = int(given[0]) if given else last + 1
            values.append(last)
        low, high = min(values, default=0), max(values, default=0)
        for size in (1, 2):
            if (0 <= low and high < 1 << 8 * size) or (-(1 << 8 * size - 1) <= low and high < 1 << 8 * size - 1):
                return size
        return 4

    def size(self, spelled: str, bound: bool = False, depth: int = 0) -> int | None:
        """Bytes of a value of the type spelled SPELLED (None: not known). A struct's size is known when every field
        is a word (or a struct of words), or, with BOUND, as an upper bound: fields aligned to their size, up to 8."""
        if depth > 32:
            return None
        bare = re.sub(r"\b(const|volatile|signed|unsigned|register)\b", "", spelled).strip()
        array = re.match(r"(.*?)\s*\[(\d+)\]$", bare)
        if array:
            inner = self.size(array[1], bound, depth + 1)
            return inner * int(array[2]) if inner else None
        if "*" in bare or "(" in bare:
            return 4
        if bare.startswith("enum "):
            return self.enums.get(bare[5:], 4)
        if bare.startswith(("struct ", "union ")):
            record = self.records.get(bare)
            if record is None:
                return None
            sizes = []
            for f in record.get("inner", []):
                if f.get("kind") != "FieldDecl":
                    continue
                if "isBitfield" in f or f.get("bitWidth"):
                    return None
                n = self.size(f["type"]["qualType"], bound, depth + 1)
                if n is None or not bound and n % 4:
                    return None
                sizes.append(n)
            if bare.startswith("union "):
                return max(sizes, default=0)
            if not bound:
                return sum(sizes)
            at = 0
            for n in sizes:
                align = min(n, 8) or 1
                at = (at + align - 1) // align * align + n
            return (at + 7) // 8 * 8 if sizes else 0
        if bare in self.typedefs:
            return self.size(self.typedefs[bare], bound, depth + 1)
        for pattern, n in self._SCALARS:
            if pattern.search(bare):
                return n
        return None


def _definition(tree: dict, name: str) -> dict | None:
    """NAME's definition in clang's TREE."""
    return next(
        (
            n
            for n in tree.get("inner", [])
            if n.get("kind") == "FunctionDecl"
            and n.get("name") == name
            and any(c.get("kind") == "CompoundStmt" for c in n.get("inner", []))
        ),
        None,
    )


def object_limit(tree: dict, name: str, types: "Types") -> int | None:
    """The size of the largest local or parameter of NAME's definition in clang's TREE whose address is taken, or of
    aggregate type (MWCC takes their addresses for copies and members); None when one's size is not known."""
    function = _definition(tree, name)
    if function is None:
        return None
    variables, taken = {}, set()

    def walk(n):
        kind = n.get("kind")
        if kind in ("VarDecl", "ParmVarDecl") and n.get("storageClass") != "static" and "id" in n:
            variables[n["id"]] = n["type"]["qualType"]
        if kind == "UnaryOperator" and n.get("opcode") == "&":
            inner = n.get("inner", [{}])[0]
            while inner.get("kind") in ("ParenExpr", "ImplicitCastExpr") and inner.get("inner"):
                inner = inner["inner"][0]
            if inner.get("kind") == "DeclRefExpr":
                taken.add(inner["referencedDecl"]["id"])
        for c in n.get("inner", []):
            if isinstance(c, dict):
                walk(c)

    walk(function)
    sizes = []
    for ident, spelled in variables.items():
        bare = spelled
        for _ in range(16):
            bare = types.typedefs.get(bare.replace("const ", "").strip(), bare)
        unqualified = re.sub(r"\b(const|volatile)\b", "", bare).strip()
        aggregate = "[" in spelled or "[" in bare or unqualified.startswith(("struct ", "union "))
        if ident in taken or aggregate:
            size = types.size(spelled, bound=True)
            if size is None:
                return None
            sizes.append(size)
    return max(sizes, default=0) or None


def indirect_words(tree: dict, name: str, types: "Types") -> int | None:
    """The most argument words any call through a function pointer in NAME's definition in clang's TREE passes (its
    arguments as converted to the pointer's parameters, rounded up to words); None when one's size is not known."""
    function = _definition(tree, name)
    most = 0

    def walk(n):
        nonlocal most
        if most is None:
            return
        if n.get("kind") == "CallExpr" and n.get("inner"):
            callee = n["inner"][0]
            while callee.get("kind") in ("ParenExpr", "ImplicitCastExpr") and callee.get("inner"):
                callee = callee["inner"][0]
            if callee.get("kind") != "DeclRefExpr" or callee.get("referencedDecl", {}).get("kind") != "FunctionDecl":
                total = 0
                for argument in n["inner"][1:]:
                    size = types.size(argument["type"]["qualType"], bound=True)
                    if size is None:
                        most = None
                        return
                    total += (size + 3) // 4
                most = max(most, total)
        for c in n.get("inner", []):
            if isinstance(c, dict):
                walk(c)

    walk(function or {})
    return most


def signatures(
    tree: dict,
) -> tuple[dict[str, int | None], dict[str, tuple[int | None, ...]], dict[str, int | None], dict[str, int | None]]:
    """From clang's TREE of a source: each function's return bits in eax and edx ({name: bits}: 0 for void, None for
    floating point, which comes back in st0), the bits of each argument word its callers push ({name: (bits or None
    for all, ...)}: a char, short or small enum parameter is a word whose low bits only the callee reads), and each
    defined function's object_limit() and indirect_words()."""
    types = Types(tree)
    returns, words = {}, {}
    limits, indirect = {}, {}
    for n in tree.get("inner", []):
        if n.get("kind") != "FunctionDecl" or "name" not in n:
            continue
        text = n["type"]["qualType"]
        returned = text[: text.index("(")].strip()
        size = types.size(returned)
        if re.search(r"\bvoid$", returned) and "*" not in returned:
            returns[n["name"]] = 0
        elif re.search(r"\b(float|double)$", re.sub(r"\bconst\b", "", returned).strip()) or size is None:
            returns[n["name"]] = None
        else:
            # (a struct of more than 8 bytes is returned through memory: nothing in the registers is taken as it)
            returns[n["name"]] = 8 * size if size <= 8 else 0
        out = []
        for parameter in (x for x in n.get("inner", []) if x.get("kind") == "ParmVarDecl"):
            size = types.size(parameter["type"]["qualType"])
            if size is None:
                break  # (the words after a parameter of unknown size are not known)
            out += [8 * size] if size < 4 else [None] * ((size + 3) // 4)
        words[n["name"]] = tuple(out)
        if any(c.get("kind") == "CompoundStmt" for c in n.get("inner", [])):
            limits[n["name"]] = object_limit(tree, n["name"], types)
            indirect[n["name"]] = indirect_words(tree, n["name"], types)
    return returns, words, limits, indirect


def context(v, words, returns):
    """What the proofs of ROW take from the rest of the program: the functions that never return, the bits each
    callee's argument words have, the bits each callee's return value has."""
    table = verdict.rows(v)
    noreturn = frozenset(r.address for r in table if r.name in NORETURN)
    parameters = {r.address: words[r.name] for r in table if words.get(r.name)}
    # (a callee returning a float or struct: taken to return all of eax, which only makes the proof stricter)
    callees = {r.address: returns[r.name] for r in table if returns.get(r.name) is not None}
    return noreturn, parameters, callees


def attempt(p, q, bits, known, disjoint, stream=None) -> proof.Result:
    """The proof of P against Q: once, then again with paths split by their flags, then with a local read before it is
    set, kept at other places, taken as one in the two (and named), until one proves."""
    noreturn, parameters, callees = known
    result = proof.prove(p, q, bits, stream, noreturn, parameters, callees, disjoint=disjoint)
    for splits, uninitialized in ((proof.SPLITS, False), (1, True), (proof.SPLITS, True)):
        if result.proved:
            break
        again = proof.prove(
            p,
            q,
            bits,
            None,
            noreturn,
            parameters,
            callees,
            uninitialized=uninitialized,
            splits=splits,
            disjoint=disjoint,
        )
        if again.proved:
            result = again
    return result


def prove_one(job) -> dict:
    v, row, bits, (returns, words, limits, indirect), trace, text = job
    # (a hard ceiling under the proof's own: a runaway proof fails with MemoryError, never takes the machine down)
    resource.setrlimit(resource.RLIMIT_AS, (proof.MEMORY * 2, resource.RLIM_INFINITY))
    known = context(v, words, returns)
    out = {"name": row.name, "address": f"{row.address:#010x}", "source": row.source}
    try:
        if isinstance(bits, str):
            raise proof.Unsupported(bits)
        if text is None:
            p, q = proof.codes(v, row, build.object_path(v, row.source))
        else:
            with isolated.Workspace() as workspace:
                compiled = workspace.compile(build.compilation(v, row.source), text)
                if not compiled.ok:
                    raise proof.Unsupported(f"the text does not compile:\n{compiled.log[-2000:]}")
                p, q = proof.codes(v, row, compiled.object)
        p.object_limit = q.object_limit = limits.get(row.name)
        p.indirect_arguments = q.indirect_arguments = indirect.get(row.name)
        # (the named assumptions hold for the build's code, not an edited text's)
        pairs = DISJOINT.get(row.name, ())[:-1] if text is None else ()
        disjoint = [(row.address + a, row.address + b) for a, b in pairs]
        with open(trace, "w") if trace else contextlib.nullcontext() as stream:
            result = attempt(p, q, bits, known, disjoint, stream)
        out.update(
            proved=result.proved,
            reason=result.reason,
            points=result.points,
            assumed={name: f"{a:#010x}" for name, a in p.assumed.items()},
            uninitialized=[f"{kp:#x}/{kq:#x}" for kp, kq in result.uninitialized],
            disjoint=[f"{pairs[k][0]:#x}/{pairs[k][1]:#x}" for k in result.disjoint],
        )
        if result.proved and len(result.disjoint) < len(pairs):
            out["unused"] = "an assumption in DISJOINT the proof did not use"
    except (proof.Differs, proof.Unsupported) as e:
        out.update(proved=False, reason=str(e))
    except Exception as e:  # noqa: BLE001 (a failure of the tool is never a proof; the rest of the run goes on)
        out.update(proved=False, reason=f"internal error: {type(e).__name__}: {e}")
    return out


def mutate_one(job) -> tuple[str, list[str], int]:
    """Mutants of one proved function's compiled code: one bit of one instruction the code reaches flipped, keeping
    its length (so the rest decodes as before); each proved as the function was. Returns the function's name, the
    mutants that still prove, and how many were tried."""
    v, row, bits, (returns, words, limits, indirect), count = job
    resource.setrlimit(resource.RLIMIT_AS, (proof.MEMORY * 2, resource.RLIM_INFINITY))
    known = context(v, words, returns)
    p, q = proof.codes(v, row, build.object_path(v, row.source))
    q.object_limit = limits.get(row.name)
    q.indirect_arguments = indirect.get(row.name)
    disjoint = [(row.address + a, row.address + b) for a, b in DISJOINT.get(row.name, ())[:-1]]
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    rng = random.Random(row.name)  # (the same mutants each run)
    reachable = sorted(a for a in p.instructions if a in p.esp)
    survived, tried = [], 0
    for _ in range(count * 50):
        if tried == count:
            break
        a = rng.choice(reachable)
        i = p.instructions[a]
        body = bytearray(p.code)
        body[a - p.address + rng.randrange(i.size)] ^= 1 << rng.randrange(8)
        new = next(md.disasm(bytes(body[a - p.address :]), a), None)
        if new is None or new.size != i.size:
            continue
        tried += 1
        text = f"{a:#x}: {i.mnemonic} {i.op_str} -> {new.mnemonic} {new.op_str}"
        try:
            m = proof.Code(p.name, p.address, bytes(body), p.image, p.jump_tables, p.constants)
            m.object_limit, m.indirect_arguments = q.object_limit, q.indirect_arguments
            if attempt(m, q, bits, known, disjoint).proved:
                survived.append(text)
        except (proof.Differs, proof.Unsupported):
            pass
        except Exception as e:  # noqa: BLE001 (a crash is not a proof: listed, the run goes on)
            print(f"{row.name} {text}: internal error: {type(e).__name__}: {e}", file=sys.stderr)
    return row.name, survived, tried


def mutate(v, chosen, count):
    """Print the mutants of the chosen functions that still prove: each is a hole in the model unless the mutated code
    still computes the same (dead code, an equivalent instruction), which only reading it can tell."""
    try:
        done = json.loads((v.build / "proofs.json").read_text())
    except FileNotFoundError:
        raise project.Error(f"no build/{v}/proofs.json: run tools/prove.py {v} first") from None
    proved = {r["name"] for r in done if r["proved"] and r["reason"] != "exact"}
    jobs, signed = [], {}
    for row in chosen:
        if row.name in proved:
            if row.source not in signed:
                signed[row.source] = signatures(clang.syntax_tree(build.compilation(v, row.source)))
            jobs.append((v, row, signed[row.source][0][row.name], signed[row.source], count))
    total = 0
    survived = 0
    for name, found, tried in isolated.parallel(mutate_one, jobs):
        total += tried
        survived += len(found)
        for text in found:
            print(f"PROVED {name} {text}")
    print(f"{survived} of {total} mutants proved (each one to read: a hole in the model, or the same function)")


def main():
    parser = argparse.ArgumentParser(
        prog="tools/prove.py",
        usage="%(prog)s [VERSION] [NAME...] [--trace FILE] [--text FILE] [--mutate [N]]",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("version", nargs="?", default=project.PRIMARY, help=f"default {project.PRIMARY}")
    parser.add_argument("names", nargs="*", metavar="NAME", help="default: every inexact function")
    parser.add_argument("--trace", metavar="FILE", help="write the pairs of program points one NAME's proof runs")
    parser.add_argument(
        "--text", metavar="FILE", help="prove one NAME compiled from FILE, an edited copy of its source"
    )
    parser.add_argument(
        "--mutate",
        nargs="?",
        type=int,
        const=40,
        metavar="N",
        help="test the prover: N mutants (default 40) of each function build/VERSION/proofs.json has proved",
    )
    args = parser.parse_args()
    try:
        if args.version not in {x.name for x in project.ALL_VERSIONS}:
            args.names.insert(0, args.version)
            args.version = project.PRIMARY
        v = project.version(args.version, original=True)
        verdicts = verdict.load(v)
        rows = {r.address: r for r in verdict.rows(v) if r.source}
        if args.names:
            by_name = {r.name: r for r in rows.values()}
            missing = [n for n in args.names if n not in by_name]
            if missing:
                raise project.Error(f"no function {', '.join(missing)} with a source in config/{v}/functions.json")
            chosen = [by_name[n] for n in args.names]
        else:
            chosen = [rows[a] for a, x in sorted(verdicts.items()) if not x.exact and a in rows]
        if (args.trace or args.text) and len(chosen) != 1:
            raise project.Error("--trace and --text take one NAME")
        text = Path(args.text).read_text(encoding="latin-1") if args.text else None
        if args.mutate is not None:
            mutate(v, chosen, args.mutate)
            return
        signed = {}
        jobs = []
        for row in chosen:
            if verdicts.get(row.address) and verdicts[row.address].exact and text is None:
                jobs.append((v, row, "exact", None, None, None))
                continue
            if row.source not in signed:
                signed[row.source] = signatures(clang.syntax_tree(build.compilation(v, row.source)))
            bits = signed[row.source][0].get(row.name, "no declaration")
            if bits is None:
                bits = "a floating-point return value, or one of a type of unknown size"
            jobs.append((v, row, bits, signed[row.source], args.trace, text))
        results = []
        for job in jobs:
            if job[2] == "exact":
                results.append(
                    {"name": job[1].name, "address": f"{job[1].address:#010x}", "proved": True, "reason": "exact"}
                )
        results += list(isolated.parallel(prove_one, [j for j in jobs if j[2] != "exact"]))
        results.sort(key=lambda r: r["address"])
        if not args.names and text is None:
            project.write_atomic(v.build / "proofs.json", json.dumps(results, indent=1) + "\n")
        for r in results:
            state = "PROVED" if r["proved"] else "not proved"
            line = f"{state:<11} {r['name']}"
            if not r["proved"] or r["reason"] == "exact":
                line += f": {r['reason']}"
            if r.get("assumed"):
                line += f" (assuming {', '.join(f'{n} at {a}' for n, a in r['assumed'].items())})"
            if r.get("uninitialized"):
                line += (
                    " (assuming no local is read before it is set at these stack offsets, compiled/original: "
                    + ", ".join(r["uninitialized"])
                    + ")"
                )
            if r.get("disjoint"):
                line += f" (assuming accesses apart, compiled offsets: {', '.join(r['disjoint'])}: see DISJOINT)"
            if r.get("unused"):
                line += f" ({r['unused']})"
            print(line)
        proved = sum(r["proved"] for r in results)
        print(f"{proved} of {len(results)} proved")
        if proved != len(results):
            sys.exit(1)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
