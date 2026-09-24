#!/usr/bin/env python3
"""Build and run the isolated Tiger surface experiment under the browser leases."""
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import uuid

root = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('tiger_run', root / 'tools/tiger-run.py')
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)
name = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-surface-' + uuid.uuid4().hex[:10]
output = root / 'logs/probes' / name
output.mkdir(parents=True)
source = root / 'spike/direct-present/surface-probe.m'
binary = output / 'TigerSurfaceProbe'
command = [str(root / 'toolchain/bin/tiger-clang'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
           str(source), '-framework', 'Cocoa', '-framework', 'OpenGL', '-framework', 'ApplicationServices',
           '-o', str(binary)]
subprocess.run(command, check=True)
remote_path = '/Users/shg/wk2/runs/' + name
remote = runner.Remote('tiger-eth')
helper = root / 'tools/tiger-run-remote.pl'
(output / 'provenance.json').write_text(json.dumps({
    'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
    'binary_sha256': hashlib.sha256(binary.read_bytes()).hexdigest(),
    'build_command': command, 'remote_run': remote_path,
    'scope': 'Cross-process GL surface and texture-upload experiment; no video playback or scanout claim.'
}, indent=2) + '\n')
print('surface probe: ' + str(output), flush=True)
with runner.local_lock(root, 600), remote.lease(name, 600):
    runner.wait_idle(remote, 300)
    remote.run(['mkdir', '-p', remote_path + '/bin'])
    remote.transfer([binary], remote_path + '/bin/')
    remote.transfer([helper], remote_path + '/')
    try:
        remote.run(['perl', remote_path + '/tiger-run-remote.pl', 'run', remote_path, '13', remote_path + '/bin/TigerSurfaceProbe'])
    finally:
        remote.run(['perl', remote_path + '/tiger-run-remote.pl', 'cleanup', remote_path])
        remote.receive(remote_path + '/app.log', output / 'app.log')
        remote.receive(remote_path + '/shot.png', output / 'shot.png')
log = (output / 'app.log').read_text(errors='replace')
print(log)
result = re.search(r'SURFACE RESULT uploads=(\d+) pixels=1280x720 elapsed=([\d.]+) rate=([\d.]+) work_mean_ms=([\d.]+) work_p95_ms=([\d.]+) work_max_ms=([\d.]+) slowest_frame=(\d+)', log)
if not result or int(result[1]) != 240 or 'SURFACE remove-owned-surface=0' not in log:
    raise SystemExit('surface probe did not complete; inspect preserved diagnostics')
(output / 'result.json').write_text(json.dumps({
    'uploads': int(result[1]), 'pixels': [1280, 720], 'elapsed_seconds': float(result[2]),
    'upload_flushes_per_second': float(result[3]), 'work_mean_ms': float(result[4]),
    'work_p95_ms': float(result[5]), 'work_max_ms': float(result[6]), 'slowest_frame': int(result[7]),
    'measurement': 'Texture uploads and CGLFlushDrawable completions, not video frames or physical scanout.'
}, indent=2) + '\n')
