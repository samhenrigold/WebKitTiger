#!/usr/bin/env python3
"""Rewrite split-sensitive conditionals in WebKit generator inputs to the TIGER_WIRE_* flags.

Both kinds of generator input are in scope: .serialization.in and .messages.in. Half of
PLATFORM(MAC)'s occurrences are in the message inputs.

A conditional in a generator input answers "is this field on the wire?"; the same macro in a .cpp
answers "do I have this framework?". The 32-bit UI process and the 64-bit content process disagree
on the second question and must agree on the first, so the inputs get their own flag set. See
logs/serializer-asymmetry.md section 1.3.

This only touches #if and #elif lines, and only the six flags below. It is deliberately dumb: the
scope comes from the probe's failure list, not from this script guessing which files matter.

  tiger-wire-remap.py --list failures.txt              # dry run, prints what would change
  tiger-wire-remap.py --list failures.txt --apply      # rewrite in place
  tiger-wire-remap.py --list failures.txt --verify     # check nothing but conditionals moved
"""
import argparse
import re
import sys
from pathlib import Path

# The six unagreeable macros and the wire flags they become, all defined 1 on every side.
#
# PLATFORM(MAC) gets its own flag rather than folding into TIGER_WIRE_APPKIT. The two ask different
# questions: TIGER_WIRE_MAC says this port is a Mac product, so the Mac-specific fields are on the
# wire and the 64-bit side carries them; TIGER_WIRE_APPKIT is about having the framework. Upstream
# keeps them apart and so do we. --mac-maps-to overrides this if that ever needs revisiting.
MAPPING = {
    "PLATFORM(COCOA)": "TIGER_WIRE_COCOA",
    "PLATFORM(MAC)": "TIGER_WIRE_MAC",
    "USE(CF)": "TIGER_WIRE_CF",
    "USE(CG)": "TIGER_WIRE_CG",
    "USE(APPKIT)": "TIGER_WIRE_APPKIT",
    "USE(CORE_TEXT)": "TIGER_WIRE_CORE_TEXT",
}

CONDITIONAL = re.compile(r"^\s*#\s*(if|elif)\b")
# On #endif and #else the flag can only appear in a trailing comment, so remapping it there is safe
# and keeps the comment from contradicting the #if it closes.
COMMENT_CARRIER = re.compile(r"^\s*#\s*(endif|else)\b")


def remap_line(line, mapping):
    out = line
    for macro, flag in mapping.items():
        out = out.replace(macro, flag)
    return out


def process(path, mapping):
    """Returns (changed_line_count, new_text, offending_non_conditional_change)."""
    original = path.read_text()
    lines = original.splitlines(keepends=True)
    changed = 0
    out = []
    for line in lines:
        if CONDITIONAL.match(line):
            new = remap_line(line, mapping)
            if new != line:
                changed += 1
            out.append(new)
        elif COMMENT_CARRIER.match(line):
            out.append(remap_line(line, mapping))  # comment only, not counted as a conditional
        else:
            # Anywhere else a flag would be a type name or prose; leave it, and --verify reports it.
            out.append(line)
    return changed, "".join(out)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--list", required=True, help="file of paths, one per line (the probe's output)")
    ap.add_argument("--root", default="WebKit-ipc", help="worktree the paths are relative to")
    ap.add_argument("--apply", action="store_true", help="rewrite in place")
    ap.add_argument("--verify", action="store_true", help="report flags left outside conditionals")
    ap.add_argument("--mac-maps-to", choices=sorted(set(MAPPING.values())),
                    help="override what PLATFORM(MAC) becomes; the default is TIGER_WIRE_MAC")
    args = ap.parse_args()

    mapping = dict(MAPPING)
    if args.mac_maps_to:
        mapping["PLATFORM(MAC)"] = args.mac_maps_to

    root = Path(args.root)
    paths = [root / line.strip() for line in Path(args.list).read_text().splitlines()
             if line.strip() and not line.startswith("#")]

    missing = [p for p in paths if not p.is_file()]
    if missing:
        print(f"error: {len(missing)} listed paths do not exist, first is {missing[0]}", file=sys.stderr)
        return 1

    totalFiles = totalLines = 0
    for path in paths:
        changed, newText = process(path, mapping)
        if not changed:
            continue
        totalFiles += 1
        totalLines += changed
        print(f"{changed:4d}  {path.relative_to(root)}")
        if args.apply:
            path.write_text(newText)

    if args.verify:
        stray = 0
        for path in paths:
            for n, line in enumerate(path.read_text().splitlines(), 1):
                if CONDITIONAL.match(line) or COMMENT_CARRIER.match(line):
                    continue
                for macro in mapping:
                    if macro in line:
                        print(f"  outside a conditional: {path.relative_to(root)}:{n}: {line.strip()}")
                        stray += 1
        print(f"\n{stray} occurrences outside #if/#elif/#endif (these are NOT remapped; check each)")

    verb = "rewrote" if args.apply else "would rewrite"
    print(f"\n{verb} {totalLines} conditionals across {totalFiles} files"
          f" (PLATFORM(MAC) -> {mapping['PLATFORM(MAC)']})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
