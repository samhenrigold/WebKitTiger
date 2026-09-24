#!/usr/bin/env python3
"""Test public below-window CGL ordering with real Tiger Cocoa controls.

Derived from the lease/provenance runner at direct-present commit 7566cf6.
--order above is the deliberate native-control-occlusion control experiment.
Focus/edit checks use Cocoa methods, not physical keyboard events or browser IPC.
"""
import argparse
import datetime
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import shutil
import subprocess
import uuid
from PIL import Image, ImageStat

ROOT = Path(__file__).resolve().parents[2]
BASE_COMMIT = '7566cf6'


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def parse_result(log, order):
    expected = -1 if order == 'below' else 1
    required = [
        'SURFACE foreground-process=0', 'SURFACE front-process=0',
        'SURFACE attach-other-process-surface=0', 'SURFACE public-surface-order=0',
        'SURFACE read-surface-order=0', 'SURFACE detach-surface=0',
        'SURFACE remove-owned-surface=0',
        f'NATIVE order requested={expected} actual={expected}',
        'NATIVE editing field-editor=1 text-match=1 selected=0,6 key=1 clipped-viewport=240x72',
    ]
    for line in required:
        if line not in log:
            raise ValueError('missing successful API check: ' + line)
    if re.search(r'^SURFACE [\w-]+=([1-9]\d*)$', log, re.M):
        raise ValueError('surface API returned an error')
    geometry = re.search(r'^NATIVE geometry origin=(\d+),(\d+) size=1280,720 ', log, re.M)
    if not geometry:
        raise ValueError('missing actual screenshot origin')
    if not re.search(r'^SURFACE RESULT mode=gl uploads=180 pixels=1280x720 ', log, re.M):
        raise ValueError('missing complete changing GPU workload')
    return {'order': order, 'content_origin': [int(geometry[1]), int(geometry[2])],
            'native_editing_api': True, 'surface_cleanup': True,
            'scope': 'Native Cocoa overlap/clipping with a separate-process GL drawable; not video or scanout.'}


def check_screenshot(path, origin, order):
    with Image.open(path) as source:
        shot = source.convert('RGB')
    x, y = origin
    if shot.width < x + 1280 or shot.height < y + 720:
        raise ValueError('screenshot does not contain the logged content rectangle')

    def region(rect):
        left, top, right, bottom = rect
        return shot.crop((x + left, y + top, x + right, y + bottom))

    def fraction(image, predicate):
        data = image.get_flattened_data() if hasattr(image, 'get_flattened_data') else image.getdata()
        data = list(data)
        return sum(predicate(*pixel) for pixel in data) / len(data)

    white = lambda r, g, b: min(r, g, b) > 190
    dark = lambda r, g, b: max(r, g, b) < 110
    gpu_red = lambda r, g, b: 48 <= r <= 112
    pink = lambda r, g, b: r > 200 and g < 60 and b > 200
    field = region((44, 78, 396, 96))
    clipped = region((204, 174, 276, 194))
    scroller = region((1243, 70, 1253, 550))
    gpu = region((700, 90, 800, 130))
    metrics = {
        'gpu_red_fraction': fraction(gpu, gpu_red),
        'gpu_blue_stddev': ImageStat.Stat(gpu).stddev[2],
        'opaque_native_magenta_fraction': fraction(region((440, 80, 600, 140)), pink),
        'field_white_fraction': fraction(field, white),
        'field_dark_fraction': fraction(field, dark),
        'field_blue_selection_fraction': fraction(field, lambda r, g, b: b > 130 and b > r * 1.25 and g > r * 1.1),
        'clipped_visible_white_fraction': fraction(clipped, white),
        'clipped_visible_dark_fraction': fraction(clipped, dark),
        'outside_clip_gpu_fraction': fraction(region((286, 174, 390, 194)), gpu_red),
        'scroller_light_fraction': fraction(scroller, lambda r, g, b: r > 150 and g > 150),
    }
    checks = {
        'GPU checker is visible': metrics['gpu_red_fraction'] > 0.95 and metrics['gpu_blue_stddev'] > 35,
        'native clipping does not leak past viewport': metrics['outside_clip_gpu_fraction'] > 0.95,
    }
    if order == 'below':
        checks.update({
            'opaque native view stays above GPU': metrics['opaque_native_magenta_fraction'] > 0.98,
            'text field white interior and glyphs remain visible': metrics['field_white_fraction'] > 0.4 and metrics['field_dark_fraction'] > 0.005,
            'field editor blue selection remains visible': metrics['field_blue_selection_fraction'] > 0.03,
            'clipped text field remains visible inside viewport': metrics['clipped_visible_white_fraction'] > 0.4 and metrics['clipped_visible_dark_fraction'] > 0.005,
            'native scroller stays above GPU': metrics['scroller_light_fraction'] > 0.5,
        })
    else:
        checks.update({
            'above-window control covers native view': metrics['opaque_native_magenta_fraction'] < 0.02,
            'above-window control covers text field': metrics['field_white_fraction'] < 0.05,
            'above-window control covers clipped field': metrics['clipped_visible_white_fraction'] < 0.05,
            'above-window control covers scroller': metrics['scroller_light_fraction'] < 0.05,
        })
    return {'passed': all(checks.values()), 'checks': checks, 'metrics': metrics}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--order', choices=('below', 'above'), default='below')
    parser.add_argument('--build-only', action='store_true')
    args = parser.parse_args()
    spec = importlib.util.spec_from_file_location('tiger_run', ROOT / 'tools/tiger-run.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    name = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-native-surface-' + args.order + '-' + uuid.uuid4().hex[:10]
    output = ROOT / 'logs/probes' / name
    output.mkdir(parents=True)
    source = output / 'surface-probe.m'
    shutil.copy2(ROOT / 'spike/native-surface/surface-probe.m', source)
    binary = output / 'TigerNativeSurfaceProbe'
    command = [str(ROOT / 'toolchain/bin/tiger-clang'), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-DSURFACE_PROBE_CA=0', str(source), '-framework', 'Cocoa', '-framework', 'OpenGL',
               '-framework', 'ApplicationServices', '-o', str(binary)]
    subprocess.run(command, check=True)
    remote_path = '/Users/shg/wk2/runs/' + name
    helper = output / 'tiger-run-remote.pl'
    shutil.copy2(ROOT / 'tools/tiger-run-remote.pl', helper)
    provenance = {
        'order': args.order, 'derived_from': BASE_COMMIT + ':spike/direct-present',
        'source_sha256': sha256(source), 'binary_sha256': sha256(binary),
        'build_command': command, 'remote_run': remote_path,
        'runner_sha256': sha256(Path(__file__)), 'remote_helper_sha256': sha256(helper),
        'lease_runner_sha256': sha256(ROOT / 'tools/tiger-run.py'),
        'scope': 'Native Cocoa overlap/clipping experiment; no browser modification.',
    }
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('native surface probe: ' + str(output), flush=True)
    if args.build_only:
        return
    remote = runner.Remote('tiger-eth')
    failure = None
    with runner.local_lock(ROOT, 600), remote.lease(name, 600):
        runner.wait_idle(remote, 300)
        remote.run(['mkdir', '-p', remote_path + '/bin'])
        remote.transfer([binary], remote_path + '/bin/')
        remote.transfer([helper], remote_path + '/')
        try:
            command = ['perl', remote_path + '/tiger-run-remote.pl', 'run', remote_path, '13', remote_path + '/bin/TigerNativeSurfaceProbe']
            if args.order == 'above':
                command.append('--above')
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
                print('native surface diagnostics: ' + str(error), flush=True)
                if failure is None:
                    failure = error
    if failure:
        raise failure
    log = (output / 'app.log').read_text(errors='replace')
    print(log)
    result = parse_result(log, args.order)
    result['screenshot'] = check_screenshot(output / 'shot.png', result['content_origin'], args.order)
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))
    if not result['screenshot']['passed']:
        raise SystemExit('native surface screenshot checks failed; inspect saved evidence')


if __name__ == '__main__':
    main()
