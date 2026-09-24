#!/usr/bin/env python3
"""Bind the native-video benchmark's parser to the verified candidate commit."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile

PARSER_PATH = 'Tools/Scripts/tiger-video-paint-stats.py'
PARSER_NAME = 'tiger-video-paint-stats.py'
LIMITS = {'warmup': 8, 'duration': 30, 'pixels': '1280x720', 'display': '1280x720', 'min_fps': 29, 'max_gap_ms': 100}


def digest(data):
    return hashlib.sha256(data).hexdigest()


def git(repo, *arguments):
    result = subprocess.run(['git', '-C', str(repo), *arguments], capture_output=True)
    if result.returncode:
        raise ValueError('candidate parser Git lookup failed: ' + result.stderr.decode(errors='replace').strip())
    return result.stdout


def write_atomic(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as output:
        temporary = Path(output.name)
        output.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def prepare(candidate_path, repo, snapshot):
    candidate = json.loads(candidate_path.read_text())
    source = candidate['source']
    head, tree = source['head'], source['tree']
    if not re.fullmatch(r'[0-9a-f]{40}', head) or not re.fullmatch(r'[0-9a-f]{40}', tree):
        raise ValueError('invalid verified candidate source identity')
    if source.get('dirty_sha256') != digest(b''):
        raise ValueError('candidate source must be committed')
    if git(repo, 'cat-file', '-t', head).strip() != b'commit':
        raise ValueError('candidate source head is not a commit')
    if git(repo, 'rev-parse', head + '^{tree}').decode().strip() != tree:
        raise ValueError('candidate source tree does not match commit')
    parser = git(repo, 'show', head + ':' + PARSER_PATH)
    blob = git(repo, 'rev-parse', head + ':' + PARSER_PATH).decode().strip()
    metadata = {'schema': 1, 'source': source, 'parser_path': PARSER_PATH,
                'parser_blob': blob, 'parser_sha256': digest(parser), 'limits': LIMITS}
    metadata_bytes = (json.dumps(metadata, indent=2, sort_keys=True) + '\n').encode()
    snapshot.mkdir(parents=True, exist_ok=True)
    for path, body in ((snapshot / PARSER_NAME, parser), (snapshot / 'parser.json', metadata_bytes)):
        if path.exists() and path.read_bytes() != body:
            raise ValueError('refusing to replace different benchmark parser snapshot: ' + str(path))
    write_atomic(snapshot / PARSER_NAME, parser)
    write_atomic(snapshot / 'parser.json', metadata_bytes)
    return metadata


def check(snapshot, log, output, mode):
    report = {'measurement': 'window-paint-not-scanout', 'status': 'gate-error', 'streams': []}
    result = None
    try:
        metadata = json.loads((snapshot / 'parser.json').read_text())
        parser = snapshot / PARSER_NAME
        if metadata.get('schema') != 1 or metadata.get('limits') != LIMITS or digest(parser.read_bytes()) != metadata.get('parser_sha256'):
            raise ValueError('benchmark parser snapshot identity/limits mismatch')
        if not log.is_file():
            raise ValueError('native-video log is missing: ' + str(log))
        command = [sys.executable, '-I', str(parser), str(log), '--warmup', str(LIMITS['warmup']), '--duration', str(LIMITS['duration']),
                   '--expect-pixels', LIMITS['pixels'], '--expect-display', LIMITS['display'], '--min-fps', str(LIMITS['min_fps']),
                   '--max-gap-ms', str(LIMITS['max_gap_ms'])]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        write_atomic(output.with_suffix('.parser-stdout.log'), result.stdout.encode())
        write_atomic(output.with_suffix('.parser-stderr.log'), result.stderr.encode())
        report = json.loads(result.stdout)
        if not isinstance(report, dict) or report.get('measurement') != 'window-paint-not-scanout':
            raise ValueError('parser did not return a window-paint report')
        streams = report.get('streams', [])
        qualifying = []
        for stream in streams:
            fps, gap = stream.get('fps'), stream.get('max_gap_ms_including_window_edges')
            qualifies = (stream.get('mode') == mode and stream.get('status') == 'certified-window-paints'
                         and isinstance(fps, (float, int)) and math.isfinite(fps) and fps >= LIMITS['min_fps']
                         and isinstance(gap, (float, int)) and math.isfinite(gap) and gap <= LIMITS['max_gap_ms'])
            if qualifies:
                qualifying.append(stream['ring'])
        passed = result.returncode == 0 and report.get('status') == 'complete' and bool(qualifying)
        report['benchmark_gate'] = {'passed': passed, 'expected_mode': mode, 'qualifying_rings': qualifying,
                                    'source_head': metadata['source']['head'], 'parser_sha256': metadata['parser_sha256'],
                                    'parser_returncode': result.returncode, 'limits': LIMITS}
    except (OSError, ValueError, KeyError, TypeError, subprocess.TimeoutExpired) as error:
        report = {'measurement': 'window-paint-not-scanout', 'status': 'gate-error', 'error': str(error),
                  'benchmark_gate': {'passed': False, 'expected_mode': mode}, 'streams': []}
        passed = False
    write_atomic(output, (json.dumps(report, indent=2, sort_keys=True) + '\n').encode())
    # Reasons remain explicit even when no sequence was certified.
    reasons = {stream.get('ring', '?'): stream.get('uncertified_attempts_by_reason', {}) for stream in report.get('streams', [])}
    print(json.dumps({'passed': passed, 'status': report['status'], 'uncertified_reasons': reasons, 'result': str(output)}, sort_keys=True))
    return passed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    command = commands.add_parser('prepare')
    command.add_argument('--candidate', type=Path, required=True)
    command.add_argument('--repo', type=Path, required=True)
    command.add_argument('--snapshot', type=Path, required=True)
    command = commands.add_parser('check')
    command.add_argument('--snapshot', type=Path, required=True)
    command.add_argument('--log', type=Path, required=True)
    command.add_argument('--output', type=Path, required=True)
    command.add_argument('--mode', choices=('fast', 'faithful'), required=True)
    args = parser.parse_args()
    try:
        if args.command == 'prepare':
            print(json.dumps(prepare(args.candidate, args.repo, args.snapshot), sort_keys=True))
            return 0
        return 0 if check(args.snapshot, args.log, args.output, args.mode) else 1
    except (OSError, ValueError, KeyError, TypeError) as error:
        print('native-video gate: ' + str(error), file=sys.stderr)
        return 1


if __name__ == '__main__':
    sys.exit(main())
