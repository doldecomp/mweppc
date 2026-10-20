#!/usr/bin/env python3

import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parent))
from mw import config, formats, link, project
from mw.pe import original


def main():
    parser = argparse.ArgumentParser(
        description="Link the relink's executable with the original's linker and check it against the original."
    )
    parser.add_argument("version", metavar="VERSION")
    parser.add_argument("output", metavar="OUTPUT")
    parser.add_argument("inputs", nargs="+", metavar="INPUT")
    args = parser.parse_args()
    version, output, inputs = args.version, args.output, args.inputs
    try:
        v = project.version(version, original=True)
        pe = original(v)
        Path(output).with_name("relink.ok").unlink(missing_ok=True)
        result = link.mwld(inputs, output, "-map", str(Path(output).with_suffix(".map")))
        if result.returncode:
            sys.exit(f"{output}: the link fails:\n" + "\n".join(link.errors(result) or [result.stdout[-2000:]]))
        linked = link.with_original_stamp(Path(output).read_bytes(), pe)
        project.write_atomic(output, linked)
        units = formats.read_splits(v.config / "splits.txt").units
        if project.digest(linked) == config.version_config(v).sha1:
            built = sum(path.startswith(f"build/{v}/compiled/") for path in inputs)
            project.write_atomic(
                Path(output).with_name("relink.ok"),
                f"{v}: the original, {built} of {len(units)} units linked from source\n",
            )
            return
        ranges = [(r.start, r.end, u.name) for u in units for r in u.ranges]
        print(f"{output}: not the original", file=sys.stderr)
        if len(linked) != len(pe.data):
            print(f"  {len(linked)} bytes, not {len(pe.data)}", file=sys.stderr)
        shown = set()
        for offset in range(min(len(linked), len(pe.data))):
            if linked[offset] == pe.data[offset]:
                continue
            section = next((s for s in pe.sections if s.file_offset <= offset < s.file_offset + s.file_size), None)
            address = section.virtual_address + offset - section.file_offset if section else None
            unit = next((u for start, end, u in ranges if address is not None and start <= address < end), "-")
            if unit not in shown:
                shown.add(unit)
                print(
                    f"  differs at {f'0x{address:08x}' if address else f'offset 0x{offset:x}'} ({unit})",
                    file=sys.stderr,
                )
                if len(shown) == 10:
                    break
        sys.exit(1)
    except project.Error as e:
        sys.exit(f"error: {e}")


if __name__ == "__main__":
    main()
