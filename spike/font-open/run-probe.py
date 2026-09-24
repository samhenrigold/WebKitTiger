#!/usr/bin/env python3
"""Build/run frozen FreeType archives on Tiger under the normal device lease."""
import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import shutil
import subprocess
import time
import uuid

ROOT = Path(__file__).resolve().parents[2]
LABELS = {('courier-normal', i) for i in range(4)} | {('courier-fork', i) for i in range(4)} | {('monaco', 0), ('courier-control', 0)}
NAMES = {'CourierNewPSMT', 'CourierNewPS-BoldMT', 'CourierNewPS-ItalicMT', 'CourierNewPS-BoldItalicMT'}
FIELDS = ('label', 'index', 'passed', 'open_error', 'name', 'faces', 'upem', 'glyph', 'size_error', 'charmap_error',
          'load_error', 'render_error', 'advance', 'width', 'rows', 'ink', 'coverage', 'bitmap_fnv1a')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def check(log, exitcode, negative=False):
    rows = {}
    summary = None
    for line in log.splitlines():
        parts = line.split('\t')
        if parts[0] == 'CASE':
            if len(parts) != len(FIELDS) + 1:
                raise ValueError('malformed case record')
            row = dict(zip(FIELDS, parts[1:]))
            for key in FIELDS:
                if key not in ('label', 'name', 'bitmap_fnv1a'):
                    row[key] = int(row[key])
            key = (row['label'], row['index'])
            if key in rows:
                raise ValueError('duplicate case')
            rows[key] = row
        elif parts[0] == 'SUMMARY':
            if summary is not None or len(parts) != 5:
                raise ValueError('duplicate or malformed summary')
            summary = [int(value) for value in parts[1:]]
    if set(rows) != LABELS or summary is None:
        raise ValueError('missing or unexpected case/summary')
    for row in rows.values():
        if not row['passed']:
            continue
        if row['passed'] != 1 or any(row[key] for key in ('open_error', 'size_error', 'charmap_error', 'load_error', 'render_error')):
            raise ValueError('successful case contains an API error')
        if any(row[key] <= 0 for key in ('faces', 'upem', 'glyph', 'advance', 'width', 'rows', 'ink', 'coverage')):
            raise ValueError('successful case lacks glyph metrics or raster pixels')
        if row['ink'] > row['width'] * row['rows'] or row['coverage'] > row['ink'] * 255:
            raise ValueError('inconsistent glyph coverage')
    for label in ('monaco', 'courier-control'):
        if not rows[(label, 0)]['passed']:
            raise ValueError('positive control failed: ' + label)
    courier = [row for row in rows.values() if row['label'].startswith('courier-') and row['label'] != 'courier-control']
    if negative:
        if exitcode != 1 or summary[0] != 9 or summary[1:] != [0, 0, 0]:
            raise ValueError('old archive did not produce the expected bounded failure')
        if any(row['passed'] or not row['open_error'] for row in courier):
            raise ValueError('negative control must reject every Courier New open')
    else:
        if exitcode or summary != [0, 15, 15, 1] or not all(row['passed'] for row in rows.values()):
            raise ValueError('rebuilt archive failed the regression')
        for label in ('courier-normal', 'courier-fork'):
            if {rows[(label, i)]['name'] for i in range(4)} != NAMES:
                raise ValueError('missing expected Courier New face')
        for i in range(4):
            for key in ('name', 'upem', 'advance', 'width', 'rows', 'bitmap_fnv1a'):
                if rows[('courier-normal', i)][key] != rows[('courier-fork', i)][key]:
                    raise ValueError('resource fork aliases differ: ' + key)
    return {'passed': True, 'expected_negative': negative, 'exitcode': exitcode,
            'summary': summary, 'cases': list(rows.values()),
            'scope': 'Real Tiger FreeType opens, Unicode glyph metrics and rasterization; no browser or native-renderer comparison.'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--without-negative-control', action='store_true')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('tiger_run', ROOT / 'tools/tiger-run.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    token = time.strftime('%Y%m%d-%H%M%S-font-open-') + uuid.uuid4().hex[:10]
    output = ROOT / 'logs/probes' / token
    output.mkdir(parents=True)
    source = output / 'font-open.c'
    shutil.copy2(Path(__file__).with_name('font-open.c'), source)
    prefix = ROOT / 'toolchain/sysroot-x86_64/usr'
    archives = {'rebuilt': prefix / 'lib/libfreetype.a'}
    if not args.without_negative_control:
        archives['before'] = ROOT / 'build/handoff/libfreetype-before-resource-attributes.a'
    # Link only these frozen copies. Toolchain and dependencies remain read-only.
    shared = []
    for name in ('libpng16.a', 'libz.a', 'libtigercompat.a'):
        copy = output / name
        shutil.copy2(prefix / 'lib' / name, copy)
        shared.append(copy)
    identity = {'host': os.environ.get('TIGER_HOST', 'tiger-eth'), 'sha256': {}, 'commands': {},
                'scope': 'No dependencies rebuilt; no font bytes copied or committed.'}
    binaries = {}
    for label, archive in archives.items():
        frozen = output / ('libfreetype-' + label + '.a')
        shutil.copy2(archive, frozen)
        binary = output / ('font-open-' + label)
        command = [str(ROOT / 'toolchain/bin/tiger-clang64'), '-std=c99', '-O2', '-g', '-Wall', '-Wextra', '-Werror',
                   '-I' + str(prefix / 'include/freetype2'), str(source), str(frozen)] + [str(path) for path in shared] + ['-o', str(binary)]
        subprocess.run(command, check=True)
        binaries[label] = binary
        identity['commands'][label] = command
        for path in (archive, frozen, binary):
            identity['sha256'][str(path.relative_to(ROOT))] = digest(path)
    for path in shared + [source, Path(__file__), ROOT / 'tools/tiger-run.py', ROOT / 'toolchain/bin/tiger-clang64',
                          prefix / 'include/freetype2/freetype/freetype.h',
                          prefix / 'include/freetype2/freetype/config/ftconfig.h',
                          prefix / 'include/freetype2/freetype/config/ftoption.h']:
        identity['sha256'][str(path.relative_to(ROOT))] = digest(path)
    (output / 'provenance.json').write_text(json.dumps(identity, indent=2) + '\n')
    print('Font-open results:', output, flush=True)
    if args.build_only:
        return 0
    remote = runner.Remote(identity['host'])
    stage = '/tmp/tiger-' + token
    results = {}
    failed = False
    with runner.local_lock(ROOT, 600), remote.lease(token, 600):
        runner.wait_idle(remote, 300)
        remote.run(['mkdir', '-m', '700', stage])
        try:
            remote.transfer(list(binaries.values()), stage + '/')
            for label, binary in binaries.items():
                remote.check_lease()
                command = ['/usr/bin/perl', '-e', 'alarm 20; exec {$ARGV[0]} @ARGV or die "exec: $!";', stage + '/' + binary.name]
                result = subprocess.run(remote.command(command), capture_output=True, timeout=35)
                (output / (label + '.stdout')).write_bytes(result.stdout)
                (output / (label + '.stderr')).write_bytes(result.stderr)
                (output / (label + '.exit')).write_text(str(result.returncode) + '\n')
                log = result.stdout.decode('utf-8', 'replace')
                print(label + ':\n' + log + result.stderr.decode('utf-8', 'replace'), flush=True)
                try:
                    results[label] = check(log, result.returncode, label == 'before')
                except ValueError as error:
                    results[label] = {'passed': False, 'error': str(error), 'exitcode': result.returncode}
                    failed = True
        finally:
            remote.run(['rm', '-f'] + [stage + '/' + binary.name for binary in binaries.values()])
            remote.run(['rmdir', stage])
    results['passed'] = not failed
    (output / 'result.json').write_text(json.dumps(results, indent=2) + '\n')
    print(json.dumps(results, indent=2))
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
