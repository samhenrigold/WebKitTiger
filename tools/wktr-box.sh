#!/bin/sh
# webkitpy's driver: keep stdin/stdout exclusively for the WKTR line protocol.
# The orchestrator supplies a verified, unique stage and holds both box leases.
# Read the embedded program from fd 3, leaving protocol stdin untouched.
exec 3<<'PY'
import os
import re
import shlex
import sys
import uuid

def main():
    stage = os.environ.get('WEBKIT_WKTR_STAGE_DIR', os.environ.get('WKTR_STAGE_DIR', ''))
    if not re.fullmatch(r'/Users/shg/wktr/runs/[A-Za-z0-9_-]+', stage):
        raise ValueError('missing verified stage; use tools/run-layout-tests-box.sh or set WKTR_STAGE_DIR to its staged run')
    host = os.environ.get('WEBKIT_WKTR_HOST', os.environ.get('TIGER_HOST', 'tiger-eth'))
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.@:-]*', host):
        raise ValueError('invalid TIGER_HOST')
    home = stage + '/home/driver-' + uuid.uuid4().hex
    fixed = {
        'HOME': home, 'TMPDIR': home + '/tmp/', 'DUMPRENDERTREE_TEMP': home + '/tmp',
        'TIGER_GPU': '0', 'TIGER_FONT_MANIFEST': stage + '/share/tiger-fonts.json',
        'TIGER_CA_BUNDLE': stage + '/share/cacert.pem',
        'WEBKIT_TIGER_HELPER_DIR': stage + '/bin',
        'DYLD_FRAMEWORK_PATH': stage + '/Frameworks',
    }
    values = {'TZ': os.environ.get('TZ', 'US/Pacific'), 'LANG': os.environ.get('LANG', 'en_US.UTF-8')}
    # webkitpy filters the parent environment. It retains WEBKIT_* and explicit
    # --additional-env-var values; preserve JSC/Tiger options supplied that way.
    for key, value in os.environ.items():
        if key.startswith(('JSC_', 'TIGER_')) and key not in fixed and key != 'TIGER_HOST':
            values[key] = value
    if 'LOCAL_RESOURCE_ROOT' in os.environ:
        values['LOCAL_RESOURCE_ROOT'] = os.environ['LOCAL_RESOURCE_ROOT']
    extra = os.environ.get('WEBKIT_WKTR_BOX_ENV', os.environ.get('WKTR_BOX_ENV', ''))
    for word in shlex.split(extra):
        key, sep, value = word.partition('=')
        if not sep or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', key):
            raise ValueError('WKTR_BOX_ENV accepts NAME=value assignments only')
        if key in fixed:
            raise ValueError('WKTR_BOX_ENV cannot override isolated runtime setting ' + key)
        values[key] = value
    values.update(fixed)
    # READY is written only after the orchestrator stages the verified binaries.
    preflight = r'''
set -eu
stage=$1
home=$2
shift 2
fail() { echo "wktr-box: $*" >&2; exit 1; }
[ -s "$stage/READY" ] || fail "stage is incomplete: $stage"
for name in WebKitTestRunner TigerWebProcess TigerNetworkProcess; do
    [ -x "$stage/bin/$name" ] || fail "missing executable: $name"
done
for name in bin/wktr-guard.pl share/tiger-fonts.json share/cacert.pem Frameworks/QuartzCore.framework/QuartzCore; do
    [ -s "$stage/$name" ] || fail "missing runtime resource: $name"
done
for name in build-manifest.json wire-messages.txt wire-serializers.txt; do
    [ -s "$stage/evidence/UI/$name" ] && [ -s "$stage/evidence/WEB/$name" ] || fail "missing build/wire evidence: $name"
done
for name in wire-messages.txt wire-serializers.txt; do
    cmp -s "$stage/evidence/UI/$name" "$stage/evidence/WEB/$name" || fail "UI/WEB wire mismatch: $name"
done
umask 077
mkdir -p "$home/tmp"
cd "$stage/bin"
exec "$@"
'''
    command = ['sh', '-c', preflight, 'wktr-box', stage, home, '/usr/bin/env']
    command += [key + '=' + value for key, value in values.items()]
    command += ['/usr/bin/nice', '-n', '10', '/usr/bin/perl', stage + '/bin/wktr-guard.pl',
                stage + '/bin/WebKitTestRunner'] + sys.argv[1:]
    ssh = ['ssh', '-T', '-o', 'BatchMode=yes', '-o', 'ExitOnForwardFailure=yes',
           '-o', 'ConnectTimeout=15', '-o', 'ServerAliveInterval=15', '-o', 'ServerAliveCountMax=3']
    for port in (8000, 8443, 8080):
        ssh += ['-R', '%d:127.0.0.1:%d' % (port, port)]
    os.execvp('ssh', ssh + [host, shlex.join(command)])

try:
    main()
except (OSError, ValueError) as error:
    print('wktr-box: ' + str(error), file=sys.stderr)
    sys.exit(1)
PY
exec python3 -c 'import os
with os.fdopen(3) as stream:
    source = stream.read()
exec(compile(source, "wktr-box.sh", "exec"))' "$@"
