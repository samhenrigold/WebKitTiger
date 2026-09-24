#!/usr/bin/env python3
"""Generate a reviewed font manifest in a unique probe directory, never install it.

Uses public Tiger ATS table identity and the existing local/remote lease and owned
process supervisor. The tracked logs/tiger-fonts.json remains unchanged. Inspect
resolution counts, native-index comparison and identity diagnostics before promotion.
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

ROOT = Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def validate_manifest(data, baseline, log):
    faces = data['faces']
    if not faces or len(faces) != data['faceCount']:
        raise ValueError('empty or incomplete font enumeration')
    resolved = 0
    changed = 0
    old = {(f['path'], f['postScriptName']): f['faceIndex'] for f in baseline['faces']}
    native_checked = 0
    for face in faces:
        index = face.get('freeTypeIndex')
        if type(index) is not int or index < -1:
            raise ValueError('missing/invalid FreeType index for ' + face['postScriptName'])
        if index >= 0:
            if not face['path'] or face['faceIndex'] < 0 or not face['unitsPerEm']:
                raise ValueError('matched face lacks native identity/metrics: ' + face['postScriptName'])
            resolved += 1
            changed += index != face['faceIndex']
        key = (face['path'], face['postScriptName'])
        if key in old:
            native_checked += 1
            if old[key] != face['faceIndex']:
                raise ValueError('native handle changed: ' + face['postScriptName'])
    if not resolved:
        raise ValueError('no exact ATS table matches')
    if (data['withFreeTypeIndex'], data['withoutFreeTypeIndex'], data['differentFaceIndices']) != (resolved, len(faces) - resolved, changed):
        raise ValueError('resolution counts disagree with emitted faces')
    native = re.search(r'self-check: (\d+) handles resolve to the named face, (\d+) do not', log)
    if not native or int(native[2]) or int(native[1]) != data['withHandle']:
        raise ValueError('native handle self-check did not complete successfully')
    return {'faces': len(faces), 'exact_table_matches': resolved, 'unresolved': len(faces) - resolved,
            'indices_differ': changed, 'native_baseline_handles_unchanged': native_checked,
            'native_resolver_self_check': int(native[1]),
            'courier_and_stone': [{key: f[key] for key in ('postScriptName', 'path', 'faceIndex', 'freeTypeIndex', 'unitsPerEm')}
                                 for f in faces if 'Courier New' in f['path'] or 'StoneSans' in f['postScriptName']]}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-only', action='store_true')
    parser.add_argument('--seconds', type=int, default=45)
    args = parser.parse_args()
    if not 10 <= args.seconds <= 180:
        parser.error('--seconds must be between 10 and 180')
    spec = importlib.util.spec_from_file_location('tiger_run', ROOT / 'tools/tiger-run.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    token = datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '-font-manifest-' + uuid.uuid4().hex[:10]
    output = ROOT / 'logs/probes' / token
    output.mkdir(parents=True)
    source = output / 'fontmanifest.c'
    binary = output / 'TigerFontManifest'
    helper = output / 'tiger-run-remote.pl'
    baseline = output / 'baseline-fonts.json'
    shutil.copy2(ROOT / 'spike/fontmanifest.c', source)
    shutil.copy2(ROOT / 'tools/tiger-run-remote.pl', helper)
    shutil.copy2(ROOT / 'logs/tiger-fonts.json', baseline)
    command = [str(ROOT / 'toolchain/bin/tiger-clang'), '-std=gnu99', '-O1', '-Wall', '-Wextra', '-Werror',
               str(source), '-ltigercompat', '-F' + str(ROOT / 'compat/sdk-overlay'),
               '-F' + str(ROOT / 'sdk/MacOSX10.4u.sdk/System/Library/Frameworks/ApplicationServices.framework/Frameworks'),
               '-framework', 'CoreFoundation', '-framework', 'CoreServices', '-framework', 'ApplicationServices',
               '-o', str(binary)]
    subprocess.run(command, check=True)
    remote_path = '/Users/shg/wk2/runs/' + token
    provenance = {'build_command': command, 'source_sha256': digest(source), 'binary_sha256': digest(binary),
                  'runner_sha256': digest(Path(__file__)), 'baseline_sha256': digest(baseline),
                  'compat_sha256': digest(ROOT / 'toolchain/sysroot-i386/usr/lib/libtigercompat.a'),
                  'remote_helper_sha256': digest(helper), 'lease_runner_sha256': digest(ROOT / 'tools/tiger-run.py'),
                  'remote_run': remote_path, 'scope': 'Generate only; no manifest installation.'}
    (output / 'provenance.json').write_text(json.dumps(provenance, indent=2) + '\n')
    print('font manifest output: ' + str(output), flush=True)
    if args.build_only:
        return
    remote = runner.Remote('tiger-eth')
    failure = None
    with runner.local_lock(ROOT, 600), remote.lease(token, 600):
        runner.wait_idle(remote, 300)
        remote.run(['mkdir', '-p', remote_path + '/bin'])
        remote.transfer([binary], remote_path + '/bin/')
        remote.transfer([helper], remote_path + '/')
        try:
            # stdout is JSON; stderr remains the supervisor's app.log. The wrapper
            # execs the unique owned binary, so normal scoped cleanup still applies.
            redirect = 'my $out = shift; open(STDOUT, ">", $out) or die $!; $ENV{FM_TRACE}=1; exec @ARGV; die $!;'
            remote.run(['perl', remote_path + '/tiger-run-remote.pl', 'run', remote_path, str(args.seconds),
                        'perl', '-e', redirect, remote_path + '/fonts.json', remote_path + '/bin/TigerFontManifest'])
        except BaseException as error:
            failure = error
        for operation in (
            lambda: remote.run(['perl', remote_path + '/tiger-run-remote.pl', 'cleanup', remote_path]),
            lambda: remote.receive(remote_path + '/app.log', output / 'app.log'),
            lambda: remote.receive(remote_path + '/fonts.json', output / 'fonts.json'),
        ):
            try:
                operation()
            except Exception as error:
                print('font manifest recovery: ' + str(error), flush=True)
                if failure is None:
                    failure = error
    if failure:
        raise failure
    result = validate_manifest(json.loads((output / 'fonts.json').read_text()),
                               json.loads(baseline.read_text()), (output / 'app.log').read_text())
    result['manifest_sha256'] = digest(output / 'fonts.json')
    (output / 'result.json').write_text(json.dumps(result, indent=2) + '\n')
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
