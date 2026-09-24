#!/usr/bin/env python3
"""Build and run the bounded realpath regression on Tiger under its normal lease."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import subprocess
import time
import uuid


def main():
    root = Path(__file__).resolve().parents[2]
    spec = importlib.util.spec_from_file_location('tiger_run', root / 'tools/tiger-run.py')
    runner = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(runner)
    token = time.strftime('%Y%m%d-%H%M%S-') + uuid.uuid4().hex[:12]
    output = root / 'logs/realpath' / token
    output.mkdir(parents=True)
    source = root / 'spike/realpath/realpath-probe.c'
    binaries = []
    identity = {'host': os.environ.get('TIGER_HOST', 'tiger-eth'), 'sha256': {}}
    for arch, compiler in (('i386', 'tiger-clang'), ('x86_64', 'tiger-clang64')):
        binary = output / ('realpath-probe-' + arch)
        subprocess.run([str(root / 'toolchain/bin' / compiler), '-O2', '-g', str(source),
                        '-ltigercompat', '-o', str(binary)], check=True)
        binaries.append(binary)
        archive = root / ('toolchain/sysroot-' + arch) / 'usr/lib/libtigercompat.a'
        for path in (binary, archive):
            identity['sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    for path in (source, root / 'compat/libcompat.c'):
        identity['sha256'][str(path.relative_to(root))] = hashlib.sha256(path.read_bytes()).hexdigest()
    (output / 'identity.json').write_text(json.dumps(identity, indent=2) + '\n')
    remote = runner.Remote(identity['host'])
    stage = '/tmp/tiger-realpath-probe-' + token
    print('Results:', output, flush=True)
    failed = False
    with runner.local_lock(root, 600), remote.lease('realpath-' + token, 600):
        runner.wait_idle(remote, 600)
        remote.run(['mkdir', '-m', '700', stage])
        try:
            remote.transfer([str(path) for path in binaries], stage + '/')
            for binary in binaries:
                # The alarm survives exec, bounding the actual remote process too.
                command = ['/usr/bin/perl', '-e', 'alarm 20; exec {$ARGV[0]} @ARGV or die "exec: $!";', stage + '/' + binary.name]
                remote.check_lease()
                result = subprocess.run(remote.command(command), capture_output=True, timeout=35)
                (output / (binary.name + '.stdout')).write_bytes(result.stdout)
                (output / (binary.name + '.stderr')).write_bytes(result.stderr)
                (output / (binary.name + '.exit')).write_text(str(result.returncode) + '\n')
                print(result.stdout.decode('utf-8', 'replace'), end='', flush=True)
                print(result.stderr.decode('utf-8', 'replace'), end='', flush=True)
                failed |= result.returncode != 0
        finally:
            remote.run(['rm', '-f'] + [stage + '/' + path.name for path in binaries])
            remote.run(['rmdir', stage])
    return int(failed)


if __name__ == '__main__':
    raise SystemExit(main())
