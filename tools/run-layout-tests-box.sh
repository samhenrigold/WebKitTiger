#!/bin/sh
# Text LayoutTests on Tiger, with a fresh profile and a verified UI/WEB test pair.
# Usage: tools/run-layout-tests-box.sh http/tests/cookies js/dom [-- harness options]
# Build both directories with TIGER_WKTR=ON first. No GPU is staged or used.
WKT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 1
exec 3<<'PY'
import importlib.util
import json
import os
from pathlib import Path
import re
import select
import shlex
import shutil
import signal
import ssl
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(sys.argv[1])

def module(name):
    spec = importlib.util.spec_from_file_location(name.replace('-', '_'), ROOT / 'tools' / (name + '.py'))
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result

runner = module('tiger-run')
wire = module('tiger-artifacts')
# Require the injected test executable, never the production WebProcess.
wire.REQUIRED = {'UI': ('WebKitTestRunner',), 'WEB': ('wktr/TigerWebProcess', 'TigerNetworkProcess')}

def manifests(ui, web):
    data = [wire.read_manifest(ui, 'UI'), wire.read_manifest(web, 'WEB')]
    if data[0]['source'] != data[1]['source']:
        raise ValueError('WKTR UI and WEB were built from different source revisions')
    if data[0]['wire'] != data[1]['wire']:
        raise ValueError('WKTR UI and WEB have different IPC message/serializer schemas')
    return data

def arguments(source):
    args = sys.argv[2:]
    cut = args.index('--') if '--' in args else len(args)
    tests, options = args[:cut], args[cut + 1:]
    if not tests:
        raise ValueError('specify targeted test paths before -- (for example http/tests/cookies js/dom)')
    controlled = ('--platform', '--child-processes', '--results-directory', '--driver-name', '--additional-driver-flag')
    for option in options:
        if option.split('=', 1)[0] in controlled:
            raise ValueError('runner controls harness option ' + option)
    paths, sync = [], set()
    layout = (source / 'LayoutTests').resolve()
    for test in tests:
        if test.startswith('-'):
            raise ValueError('put harness options after --: ' + test)
        path = (layout / test).resolve()
        relative = path.relative_to(layout)
        if not relative.parts or not path.exists():
            raise ValueError('test path must exist below LayoutTests: ' + test)
        paths.append(relative.as_posix())
        # Single files can load sibling fixtures, so also stage their directory.
        support = path if path.is_dir() else path.parent
        sync.add(support.relative_to(layout).as_posix())
        for parent in [support] + list(support.parents):
            if parent == layout.parent:
                break
            resources = parent / 'resources'
            if resources.is_dir():
                sync.add(resources.relative_to(layout).as_posix())
    for name in ('resources', 'http/tests/resources', 'js/resources'):
        if (layout / name).is_dir():
            sync.add(name)
    sync = [name for name in sorted(sync) if not any(name != p and (p == '.' or name.startswith(p + '/')) for p in sync)]
    return paths, options, layout, sync

def snapshot(destination, ui, web, source):
    data = manifests(ui, web)  # Fail locally before a lease or any remote probe.
    head = runner.checked(['git', '-C', source, 'rev-parse', 'HEAD'], capture_output=True, text=True).stdout.strip()
    if head != data[0]['source']['head']:
        raise ValueError('LayoutTests checkout differs from the WKTR build revision')
    stage = destination / 'stage'
    (stage / 'bin').mkdir(parents=True)
    (stage / 'share').mkdir()
    (stage / 'Frameworks').mkdir()
    for directory, manifest in zip((ui, web), data):
        copy = destination / manifest['process']
        for name in manifest['binaries']:
            target = copy / 'bin' / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(directory / 'bin' / name, target)
        for name in ('build-manifest.json', 'wire-messages.txt', 'wire-serializers.txt'):
            shutil.copy2(directory / name, copy / name)
    data = manifests(destination / 'UI', destination / 'WEB')  # Recheck frozen bytes.
    for process, names in wire.REQUIRED.items():
        for name in names:
            shutil.copy2(destination / process / 'bin' / name, stage / 'bin' / Path(name).name)
        evidence = stage / 'evidence' / process
        evidence.mkdir(parents=True)
        for name in ('build-manifest.json', 'wire-messages.txt', 'wire-serializers.txt'):
            shutil.copy2(destination / process / name, evidence / name)
    shutil.copy2(ROOT / 'tools/wktr-guard.pl', stage / 'bin/wktr-guard.pl')
    shutil.copy2(ROOT / 'logs/tiger-fonts.json', stage / 'share/tiger-fonts.json')
    framework = ROOT / 'spike/CAHost/Frameworks/QuartzCore.framework'
    if not (framework / 'QuartzCore').is_file():
        raise ValueError('missing QuartzCore framework binary')
    shutil.copytree(framework, stage / 'Frameworks/QuartzCore.framework', symlinks=True)
    # This PEM includes a private key. Transfer only its public certificates.
    pem = (source / 'LayoutTests/http/conf/webkit-httpd.pem').read_text()
    blocks = re.findall(r'-----BEGIN CERTIFICATE-----.*?-----END CERTIFICATE-----', pem, re.S)
    if not blocks:
        raise ValueError('WKTR HTTPS certificate missing from webkit-httpd.pem')
    bundle = (ROOT / 'deps/src/cacert.pem').read_text().rstrip() + '\n' + '\n'.join(blocks) + '\n'
    ca = stage / 'share/cacert.pem'
    ca.write_text(bundle)
    ssl.create_default_context(cafile=str(ca))  # Reject malformed certificate bundles.
    return stage, data

def cleanup(remote, stage):
    # The guard normally handles this. After a harness interruption, signal only
    # this unique stage's guard, never another run or the user's browser.
    program = r'''
use strict;
my $guard = $ARGV[0] . '/bin/wktr-guard.pl';
open(my $ps, '-|', '/bin/ps', '-axww', '-o', 'pid,command') or die "ps: $!";
while (<$ps>) {
    if (/^\s*(\d+)\s+\/usr\/bin\/perl\s+\Q$guard\E(?:\s|$)/) { kill 'TERM', $1; }
}
close $ps;
'''
    remote.run(['perl', '-e', program, stage, 'cleanup'])

def run_harness(command, environment, log, remote):
    with log.open('wb') as output:
        proc = subprocess.Popen(command, env=environment, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            while True:
                remote.check_lease()
                ready, _, _ = select.select([proc.stdout], [], [], 1)
                if not ready:
                    continue
                chunk = os.read(proc.stdout.fileno(), 65536)
                if not chunk:
                    return proc.wait()
                output.write(chunk)
                output.flush()
                sys.stdout.buffer.write(chunk)
                sys.stdout.buffer.flush()
        finally:
            if proc.poll() is None:
                proc.terminate()
                try:
                    proc.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    proc.wait()
            proc.stdout.close()

def main():
    source = runner.build_dir(ROOT, os.environ, 'WKTR_SOURCE', 'WebKit-tests')
    ui = runner.build_dir(ROOT, os.environ, 'WKTR_UIDIR', 'build/tiger-ui-tests')
    web = runner.build_dir(ROOT, os.environ, 'WKTR_WEBDIR', 'build/tiger-web-tests')
    tests, options, layout, sync = arguments(source)
    extra = os.environ.get('WKTR_BOX_ENV', '')
    # Explicit harness environment options must reach the remote process too,
    # including variables outside webkitpy's usual JSC/WEBKIT prefixes.
    for index, option in enumerate(options):
        assignment = None
        if option == '--additional-env-var':
            if index + 1 == len(options):
                raise ValueError('--additional-env-var needs NAME=value')
            assignment = options[index + 1]
        elif option.startswith('--additional-env-var='):
            assignment = option.split('=', 1)[1]
        if assignment is not None:
            if assignment.startswith('WEBKIT_WKTR_'):
                raise ValueError('runner controls WEBKIT_WKTR_* environment')
            extra += ' ' + shlex.quote(assignment)
    for word in shlex.split(extra):
        key, sep, value = word.partition('=')
        if not sep or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', key):
            raise ValueError('WKTR_BOX_ENV accepts NAME=value assignments only')
        if key in ('HOME', 'TMPDIR', 'DUMPRENDERTREE_TEMP', 'TIGER_GPU', 'TIGER_FONT_MANIFEST', 'TIGER_CA_BUNDLE', 'WEBKIT_TIGER_HELPER_DIR', 'DYLD_FRAMEWORK_PATH'):
            raise ValueError('WKTR_BOX_ENV cannot override isolated runtime setting ' + key)
    token = time.strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:12]
    stage = '/Users/shg/wktr/runs/' + token
    result = ROOT / 'logs/layout-tests' / token
    remote = runner.Remote(os.environ.get('TIGER_HOST', 'tiger-eth'))
    timeout = int(os.environ.get('WKTR_LOCK_TIMEOUT', '600'))
    if timeout < 0:
        raise ValueError('WKTR_LOCK_TIMEOUT must be nonnegative')
    with tempfile.TemporaryDirectory(prefix='tiger-wktr-') as temp:
        frozen, data = snapshot(Path(temp), ui, web, source)
        result.mkdir(parents=True)
        shutil.copytree(frozen / 'evidence', result / 'evidence')
        (result / 'run.json').write_text(json.dumps({'stage': stage, 'host': remote.host, 'source': str(source),
            'tests': tests, 'options': options, 'wire': data[0]['wire'], 'gpu': False}, indent=2) + '\n')
        with runner.local_lock(ROOT, timeout), remote.lease(token, timeout):
            runner.wait_idle(remote, timeout)
            table = remote.run(['ps', '-axww', '-o', 'pid,command'], capture_output=True, text=True).stdout
            if re.search(r'(?:^|[/\s])WebKitTestRunner(?:\s|$)', table):
                raise ValueError('a WebKitTestRunner is already using Tiger; nothing launched')
            remote.run(['mkdir', '-p', stage, str(layout)])
            remote.transfer([str(frozen) + '/'], stage + '/')
            # File URLs retain their absolute checkout path on both machines.
            runner.checked(['rsync', '-rtlR', '-z', '--bwlimit=20000', '--exclude=*-expected.*', '--'] + sync +
                [remote.host + ':' + shlex.quote(str(layout) + '/')], cwd=layout)
            remote.run(['sh', '-c', 'umask 077; printf "%s\\n" "$2" > "$1/READY"', 'wktr-ready', stage, token])
            environment = os.environ.copy()
            # WEBKIT_* survives webkitpy's intentional environment filtering.
            environment.update(WEBKITTIGER_ROOT=str(ROOT), WEBKIT_WKTR_STAGE_DIR=stage,
                               WEBKIT_WKTR_HOST=remote.host, WEBKIT_WKTR_BOX_ENV=extra)
            command = [sys.executable, str(source / 'Tools/Scripts/run-webkit-tests')] + options + [
                '--platform', 'tiger', '--no-build', '--no-show-results', '--child-processes', '1',
                '--results-directory', str(result)] + tests
            try:
                status = run_harness(command, environment, result / 'run.log', remote)
            finally:
                cleanup(remote, stage)
            print('results: ' + str(result), flush=True)
            return status

def interrupted(signum, frame):
    raise KeyboardInterrupt()

signal.signal(signal.SIGTERM, interrupted)
try:
    sys.exit(main())
except KeyboardInterrupt:
    sys.exit(130)
except (OSError, ValueError, runner.RunError, subprocess.CalledProcessError) as error:
    print('run-layout-tests-box: ' + str(error), file=sys.stderr)
    sys.exit(1)
PY
exec python3 -c 'import os
with os.fdopen(3) as stream:
    source = stream.read()
exec(compile(source, "run-layout-tests-box.sh", "exec"))' "$WKT" "$@"
