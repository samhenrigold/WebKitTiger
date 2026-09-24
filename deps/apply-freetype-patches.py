#!/usr/bin/env python3
"""Apply the maintained Tiger FreeType fixes, rejecting unknown source drift."""
from pathlib import Path
import subprocess
import sys

if len(sys.argv) != 2:
    raise SystemExit('usage: apply-freetype-patches.py <FreeType source directory>')
source = Path(sys.argv[1]).resolve()
patch = Path(__file__).resolve().parent / 'patches/freetype-resource-attributes.patch'
arguments = ['patch', '-p1', '--fuzz=0', '-i', str(patch)]
forward = subprocess.run(arguments + ['--dry-run', '--forward'], cwd=source, capture_output=True, text=True)
if forward.returncode == 0:
    subprocess.run(arguments + ['--forward'], cwd=source, check=True)
else:
    reverse = subprocess.run(arguments + ['--dry-run', '--reverse'], cwd=source, capture_output=True, text=True)
    if reverse.returncode:
        raise SystemExit('FreeType patch does not match this source:\n' + forward.stdout + forward.stderr)
    print('FreeType resource attributes patch already applied')
