#!/usr/bin/env python3
"""Build/run an isolated GL or CARenderer surface probe under the browser leases."""
import argparse
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import sys
import uuid

ROOT = Path(__file__).resolve().parents[2]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_result(log, mode, remote_path):
    pattern = (r'SURFACE RESULT mode=(gl|ca) uploads=(\d+) pixels=1280x720 elapsed=([\d.]+) '
               r'rate=([\d.]+) work_mean_ms=([\d.]+) work_p95_ms=([\d.]+) work_max_ms=([\d.]+) '
               r'slowest_frame=(\d+) copy_mean_ms=([\d.]+) render_mean_ms=([\d.]+) '
               r'flush_mean_ms=([\d.]+) completed_at=([\d.]+) deadline=([\d.]+)')
    results = list(re.finditer(pattern, log))
    if len(results) != 1:
        raise ValueError('expected exactly one complete SURFACE RESULT')
    result = results[0]
    if result[1] != mode or int(result[2]) != 240:
        raise ValueError('wrong mode or incomplete 240-frame workload')
    if 'SURFACE detach-surface=0' not in log or 'SURFACE remove-owned-surface=0' not in log:
        raise ValueError('surface cleanup did not complete')
    if float(result[12]) > float(result[13]) or float(result[13]) > 8.5:
        raise ValueError('work exceeded pre-screenshot deadline; timings are not accepted')
    if mode == 'ca':
        expected = remote_path + '/bin/../Frameworks/QuartzCore.framework/Versions/A/QuartzCore'
        normalized = remote_path + '/Frameworks/QuartzCore.framework/Versions/A/QuartzCore'
        paths = re.findall(r'^SURFACE quartzcore_path=(.+)$', log, re.M)
        if paths not in ([expected], [normalized]):
            raise ValueError('CA did not load the framework copied into this run')
        if 'SURFACE overlay=opaque-magenta rect=40,40,200,72 above=fresh-video-image' not in log:
            raise ValueError('missing CA overlay scene')
    return {
        'mode': mode, 'uploads': int(result[2]), 'pixels': [1280, 720],
        'elapsed_seconds': float(result[3]), 'completed_flushes_per_second': float(result[4]),
        'work_mean_ms': float(result[5]), 'work_p95_ms': float(result[6]),
        'work_max_ms': float(result[7]), 'slowest_frame': int(result[8]),
        'copy_mean_ms': float(result[9]), 'render_mean_ms': float(result[10]),
        'flush_mean_ms': float(result[11]), 'completed_at_seconds': float(result[12]),
        'measurement_deadline_seconds': float(result[13]),
        'measurement': ('Fresh owned CGImage copies, full CA scene renders and CGLFlushDrawable completions'
                        if mode == 'ca' else 'Texture uploads and CGLFlushDrawable completions')
                       + '; not decoded video delivery or physical scanout.',
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mode', choices=('gl', 'ca'), default='gl')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('tiger_run', ROOT / 'tools/tiger-run.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    name = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-surface-' + args.mode + '-' + uuid.uuid4().hex[:10]
    output = ROOT / 'logs/probes' / name
    output.mkdir(parents=True)
    source = output / 'surface-probe.m'
    shutil.copy2(ROOT / 'spike/direct-present/surface-probe.m', source)
    binary = output / 'TigerSurfaceProbe'
    command = [str(ROOT / 'toolchain/bin/tiger-clang'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DSURFACE_PROBE_CA=' + ('1' if args.mode == 'ca' else '0'), str(source),
               '-framework', 'Cocoa', '-framework', 'OpenGL', '-framework', 'ApplicationServices']
    framework = None
    framework_provenance = None
    if args.mode == 'ca':
        framework = output / 'Frameworks/QuartzCore.framework'
        shutil.copytree(ROOT / 'spike/CAHost/Frameworks/QuartzCore.framework', framework, symlinks=True)
        files = {}
        for path in sorted(framework.rglob('*')):
            key = str(path.relative_to(framework))
            if path.is_symlink():
                files[key] = {'symlink': str(path.readlink())}
            elif path.is_file():
                files[key] = {'sha256': sha256(path)}
        framework_provenance = {
            'origin': 'spike/CAHost/Frameworks/QuartzCore.framework',
            'binary_sha256': sha256(framework / 'Versions/A/QuartzCore'),
            'files': files,
        }
        command += ['-F' + str(framework.parent), '-framework', 'QuartzCore']
    command += ['-o', str(binary)]
    subprocess.run(command, check=True)
    remote_path = '/Users/shg/wk2/runs/' + name
    helper = ROOT / 'tools/tiger-run-remote.pl'
    provenance = {
        'mode': args.mode, 'source_sha256': sha256(source), 'binary_sha256': sha256(binary),
        'build_command': command, 'remote_run': remote_path,
        'framework': framework_provenance, 'remote_helper_sha256': sha256(helper),
        'scope': 'Isolated imported drawable experiment; no video playback or scanout claim.',
    }
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('surface probe: ' + str(output), flush=True)
    if args.build_only:
        return
    remote = runner.Remote('tiger-eth')
    failure = None
    with runner.local_lock(ROOT, 600), remote.lease(name, 600):
        runner.wait_idle(remote, 300)
        remote.run(['mkdir', '-p', remote_path + '/bin', remote_path + '/Frameworks'])
        remote.transfer([binary], remote_path + '/bin/')
        if framework:
            remote.transfer([framework], remote_path + '/Frameworks/')
        remote.transfer([helper], remote_path + '/')
        try:
            command = ['perl', remote_path + '/tiger-run-remote.pl', 'run', remote_path, '13', remote_path + '/bin/TigerSurfaceProbe']
            if args.mode == 'ca':
                command.append('--ca')
            remote.run(command)
        except BaseException as error:
            failure = error
        for operation in (
            lambda: remote.run(['perl', remote_path + '/tiger-run-remote.pl', 'cleanup', remote_path]),
            lambda: remote.receive(remote_path + '/app.log', output / 'app.log'),
            lambda: remote.receive(remote_path + '/shot.png', output / 'shot.png'),
        ):
            try:
                operation()
            except Exception as error:
                print('surface probe diagnostics: ' + str(error), file=sys.stderr)
                if failure is None:
                    failure = error
    if failure:
        raise failure
    log = (output / 'app.log').read_text(errors='replace')
    print(log)
    result = parse_result(log, args.mode, remote_path)
    result['framework_binary_sha256'] = framework_provenance['binary_sha256'] if framework_provenance else None
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')


if __name__ == '__main__':
    main()
