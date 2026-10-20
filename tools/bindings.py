#!/usr/bin/env python3
"""python tools/bindings.py COMMAND ...: config/VERSION/bindings.json against what the original's code says. Each
reads the build's objects: run ninja first.

  learn VERSION... [--write]
      The address of each external symbol the sources reference without a binding, where functions that are the
      original's but for their relocated fields all say the same: a binding to add (--write adds them). Not learned,
      and listed: an address that is a row's (the code calls a function by another name than its row's), an address
      bound under another name (the two names are one variable: rename one), symbols the functions disagree about.
  audit VERSION [--fix]
      Each bound name that every reference from such functions puts at one other address: a wrong binding (--fix moves
      it there). A row whose callers all call another address is listed too (swapped row names), never fixed.
  infer SOURCE [--version V] [--min-votes N] [--write]
      Like learn, for one source and from all its functions: an inexact function's references count where its code
      and the original's line up around them. Each symbol with the address its references agree on (at least N, default
      2, or all of them when one function references it) and the names already there: when SOURCE is written with the
      reference's names (Pro7's), those are the repository's stand-ins for the same variable (rename them). --write
      adds the agreed symbols nothing is bound at.
  refs VERSION ADDRESS...
      The original's functions with an absolute reference to each ADDRESS (a field at an offset into a struct is
      another address).
  refs --wii [--exe EXE] NAME...  /  refs --wii [--exe EXE] --function FUNCTION...
      The Wii compiler's functions referencing each data public NAME, or the data publics each Wii FUNCTION
      references (C names; default EXE build/compilers/Wii/1.0RC1/mwcceppc.exe). Two names bound at one address: the
      functions that reference it in the original, against the ones that reference a Wii public, say which it is.
"""

import argparse
import bisect
import collections
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import config, project, verdict, wii
from mw.names import c_name, is_import
from mw.pe import original


def hexes(counter):
    return ", ".join(f"0x{a:08x}" + (f" x{n}" if n > 1 else "") for a, n in counter.most_common())


def names_at(version):
    """{address: [names]} of the bindings and rows."""
    out = collections.defaultdict(list)
    for name, address in config.bindings(version).items():
        if not is_import(name):
            out[address].append(name)
    for row in config.functions(version):
        out[row.address].append(f"{row.name} (row)")
    return out


def votes_of(references):
    votes = collections.defaultdict(collections.Counter)
    users = collections.defaultdict(set)
    for r in references:
        votes[r.symbol][r.original] += 1
        users[r.symbol].add(r.row.name)
    return votes, users


def write_bindings(version, learned):
    if not learned:
        return
    with project.Transaction() as tx:
        path = version.config / "bindings.json"
        table = config.bindings(version, tx.read(path))
        table.update(learned)
        tx.write(path, config.dump_bindings(table))
    print(f"{version}: {len(learned)} bindings written")


def learn(versions, write):
    for v in versions:
        refs = [r for r in verdict.references(v) if not r.defined and r.bound is None and r.plausible]
        votes, _ = votes_of(refs)
        there = names_at(v)
        learned, disputed = {}, []
        for symbol, counter in sorted(votes.items()):
            if len(counter) > 1:
                disputed.append(f"  {symbol}: {hexes(counter)}")
                continue
            ((address, _),) = counter.items()
            if there.get(address):
                print(f"{v}: {symbol}: 0x{address:08x} is {', '.join(there[address])}")
            else:
                learned[symbol] = address
                print(f"{v}: {symbol} = 0x{address:08x}")
        if disputed:
            print(f"{v}: the functions disagree about\n" + "\n".join(disputed))
        print(f"{v}: {len(learned)} to learn, {len(disputed)} disputed")
        if write:
            write_bindings(v, learned)


def audit(version, fix):
    table = config.bindings(version)
    rows = {f"_{r.name}" for r in config.functions(version)}
    refs = [r for r in verdict.references(version) if r.bound is not None and r.plausible]
    votes, users = votes_of(refs)
    bound_at = {r.symbol: r.bound for r in refs}
    wrong = {}
    for symbol, counter in sorted(votes.items()):
        if len(counter) > 1:
            continue
        ((address, count),) = counter.items()
        bound = bound_at[symbol]
        if address == bound:
            continue
        key = symbol if symbol in table else "_" + c_name(symbol)
        if key in table and symbol not in rows and key not in rows:
            wrong[key] = address
            print(
                f"{key}: bound at 0x{bound:08x}, the original refers to 0x{address:08x} ({count} references: "
                f"{', '.join(sorted(users[symbol])[:4])})"
            )
        else:
            print(
                f"{symbol}: the row is at 0x{bound:08x}, its callers call 0x{address:08x} "
                f"({', '.join(sorted(users[symbol])[:4])})"
            )
    print(f"{version}: {len(wrong)} bindings every reference disagrees with")
    if fix and wrong:
        with project.Transaction() as tx:
            path = version.config / "bindings.json"
            current = config.bindings(version, tx.read(path))
            current.update(wrong)
            tx.write(path, config.dump_bindings(current))
        print(f"{version}: {len(wrong)} bindings moved")


def infer(version, source, min_votes, write):
    if source not in config.sources():
        raise project.Error(f"{source} is not in config/sources.json")
    refs = [r for r in verdict.references(version, [source], aligned=True) if r.bound is None]
    defined = {r.symbol for r in refs if r.defined}
    votes, users = votes_of(refs)
    there = names_at(version)
    learned = {}
    for symbol, counter in sorted(votes.items()):
        (address, count), *rest = counter.most_common()
        agreed = not rest and (count >= min_votes or len(users[symbol]) == 1)
        others = there.get(address, [])
        mark = "" if agreed else f"  (disputed: {hexes(counter)})"
        print(
            f"{symbol:40s} 0x{address:08x}  {', '.join(others) or '-'}{mark}"
            f"{'  (defined by the source)' if symbol in defined else ''}"
        )
        if agreed and not others and symbol not in defined:
            learned[symbol] = address
    if write:
        write_bindings(version, learned)


class Referrers:
    """The functions of an executable with an absolute reference to an address (its base-relocated words)."""

    def __init__(self, pe, functions):
        self.functions = sorted(functions)  # [(start, end, label)]
        self.starts = [f[0] for f in self.functions]
        self.sites = collections.defaultdict(list)  # value -> [site]
        for site in pe.base_relocations:
            self.sites[pe.word(site)].append(site)

    def of(self, address):
        hits = set()
        for site in self.sites.get(address, []):
            i = bisect.bisect_right(self.starts, site) - 1
            if i >= 0 and site < self.functions[i][1]:
                hits.add(self.functions[i][2])
        return sorted(hits)


def refs(version, addresses):
    referrers = Referrers(
        original(version),
        [
            (r.address, r.end, f"{r.name} ({Path(r.source).name if r.source else '-'})")
            for r in config.functions(version)
        ],
    )
    for address in addresses:
        print(f"0x{address:08x}: {', '.join(referrers.of(address)) or '-'}")


def wii_refs(exe, names, functions):
    image = wii.read(exe)
    labels = image.names()
    code = [
        (c.address, c.address + c.size, labels.get(c.address, f"0x{c.address:08x} ({c.module})"))
        for c in image.functions()
    ]
    data = {c_name(p.name): p.address for p in image.publics if not p.code}
    referrers = Referrers(image.pe, code)
    for name in names:
        if name in data:
            print(f"{name} (0x{data[name]:08x}): {', '.join(referrers.of(data[name])) or '-'}")
        else:
            print(f"{name}: not a data public")
    by_address = {address: name for name, address in data.items()}
    extent = {label: (start, end) for start, end, label in code}
    fixups = image.pe.base_relocations
    for name in functions:
        if name not in extent:
            print(f"{name}: not a public function")
            continue
        start, end = extent[name]
        found = {
            by_address[w]
            for site in fixups[bisect.bisect_left(fixups, start) : bisect.bisect_left(fixups, end)]
            if (w := image.pe.word(site)) in by_address
        }
        print(f"{name}: {', '.join(sorted(found)) or '-'}")


def main():
    parser = argparse.ArgumentParser(
        prog="tools/bindings.py",
        usage="%(prog)s learn|audit|infer|refs ...",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    commands = parser.add_subparsers(dest="command", required=True, metavar="COMMAND")
    p = commands.add_parser("learn")
    p.add_argument("versions", nargs="+")
    p.add_argument("--write", action="store_true")
    p = commands.add_parser("audit")
    p.add_argument("version")
    p.add_argument("--fix", action="store_true")
    p = commands.add_parser("infer")
    p.add_argument("source")
    p.add_argument("--version", default=project.PRIMARY)
    p.add_argument("--min-votes", type=int, default=2)
    p.add_argument("--write", action="store_true")
    p = commands.add_parser("refs")
    p.add_argument("args", nargs="+")
    p.add_argument("--wii", action="store_true")
    p.add_argument("--exe", default=wii.DEFAULT)
    p.add_argument("--function", action="store_true")
    args = parser.parse_args()
    try:
        if args.command == "learn":
            learn([project.version(v) for v in args.versions], args.write)
        elif args.command == "audit":
            audit(project.version(args.version), args.fix)
        elif args.command == "infer":
            infer(project.version(args.version), args.source, args.min_votes, args.write)
        elif args.wii:
            wii_refs(args.exe, [] if args.function else args.args, args.args if args.function else [])
        else:
            if len(args.args) < 2:
                parser.error("refs VERSION ADDRESS...")
            try:
                addresses = [int(a, 16) for a in args.args[1:]]
            except ValueError:
                parser.error(f"refs takes hexadecimal addresses, not {' '.join(args.args[1:])}")
            refs(project.version(args.args[0]), addresses)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
