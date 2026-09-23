#!/usr/bin/env python3
"""Fingerprint the actual build's IPC, and reject incomplete/mixed Tiger bundles."""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import sys


def digest(path):
    value = hashlib.sha256()
    with Path(path).open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            value.update(chunk)
    return value.hexdigest()


def run(argv, cwd, data=None):
    result = subprocess.run(argv, cwd=cwd, input=data, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise ValueError('command failed: {}\n{}'.format(shlex.join(argv), result.stderr[-6000:]))
    return result.stdout


def cache_value(build, name):
    for line in (build / 'CMakeCache.txt').read_text().splitlines():
        if line.startswith(name + ':'):
            return line.split('=', 1)[1]
    raise ValueError('{} missing from {}'.format(name, build / 'CMakeCache.txt'))


def preprocess_command(command):
    """Retain real compiler/launcher/defines/includes; remove output/dependency/PCH flags."""
    args = shlex.split(command)
    if args[:2] == [':', '&&']:
        args = args[2:]
    if args[-2:] == ['&&', ':']:
        args = args[:-2]
    if any(arg in ('&&', '||', ';', '|', '>') for arg in args):
        raise ValueError('unexpected shell operator in compiler command')
    result, source, index = [], None, 0
    while index < len(args):
        arg = args[index]
        if arg == '-Xclang' and args[index + 1:index + 2] in (['-include-pch'], ['-include']):
            if args[index + 2:index + 3] != ['-Xclang'] or index + 3 >= len(args):
                raise ValueError('unexpected precompiled-header flags')
            index += 4
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ', '-c'):
            if index + 1 >= len(args):
                raise ValueError('missing compiler argument after ' + arg)
            if arg == '-c':
                source = args[index + 1]
            index += 2
            continue
        if arg not in ('-MD', '-MMD', '-MP'):
            result.append(arg)
        index += 1
    if not source:
        raise ValueError('compiler command has no source')
    return result + ['-E'], source


def message_names(expanded):
    match = re.search(r'enum class MessageName\s*:\s*uint16_t\s*\{(.*?)\};', expanded, re.S)
    if not match:
        raise ValueError('preprocessor did not produce MessageName enum')
    messages = []
    # Conditional declarations introduce preprocessor line markers inside the enum.
    body = re.sub(r'^\s*#.*$', '', match.group(1), flags=re.M)
    for item in body.split(','):
        item = ' '.join(item.split())
        if item:
            if not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*(?:\s*=\s*(?:[0-9]+|[A-Za-z_][A-Za-z0-9_]*)(?:\s*[+-]\s*[0-9]+)?)?', item):
                raise ValueError('unexpected message enumerator: ' + item)
            messages.append(item)
    return messages


def fingerprint(build, out):
    build, out = build.resolve(), out.resolve()
    ninja = cache_value(build, 'CMAKE_MAKE_PROGRAM')
    source_dir = Path(cache_value(build, 'CMAKE_HOME_DIRECTORY'))
    targets = run([ninja, '-t', 'targets', 'all'], build)
    objects = {}
    for line in targets.splitlines():
        target = line.split(': ', 1)[0]
        name = re.search(r'(GeneratedSerializers[A-Za-z]+\.cpp)\.o$', target)
        if name and '/WebKitShared.dir/' in target:
            objects.setdefault(name.group(1), target)
    if not objects or 'GeneratedSerializersCommon.cpp' not in objects:
        raise ValueError('no complete WebKitShared serializer targets in ' + str(build))
    schemas, contract, first_command = [], '', None
    for name, target in sorted(objects.items()):
        commands = run([ninja, '-t', 'commands', target], build).splitlines()
        command, source = preprocess_command(commands[-1])
        first_command = first_command or command
        expanded = run(command + [source], build)
        # The native adapter audit needs the effective declarations, not just raw headers.
        if name == 'GeneratedSerializersShared.cpp':
            contract = expanded
        schemas.append('#### ' + name)
        for line in expanded.splitlines():
            header = re.search(r'std::optional<(.*)> ArgumentCoder<.*>::decode\(', line)
            member = re.search(r'auto ([A-Za-z0-9_]+) = decoder\.decode<(.*)>\(\);', line)
            if header:
                schemas.append('== ' + header.group(1))
            elif member:
                schemas.append('  {} : {}'.format(*member.groups()))
    if sum(line.startswith('== ') for line in schemas) < 100:
        raise ValueError('unexpectedly incomplete serializer output')
    expanded = run(first_command + ['-x', 'c++', '-'], build,
                   '#include "config.h"\n#include "MessageNames.h"\n')
    messages = message_names(expanded)
    if len(messages) < 100:
        raise ValueError('unexpectedly incomplete message table')
    schema_path = Path(__file__).with_name('tiger-wire-schema.py')
    spec = importlib.util.spec_from_file_location('tiger_wire_schema', schema_path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    normalized = module.normalize_serializers('\n'.join(schemas) + '\n', source_dir, contract)
    out.mkdir(parents=True, exist_ok=True)
    (out / 'wire-messages.txt').write_text('\n'.join(messages) + '\n')
    (out / 'wire-serializers.txt').write_text(normalized)
    return {'messages_sha256': digest(out / 'wire-messages.txt'),
            'serializers_sha256': digest(out / 'wire-serializers.txt')}


REQUIRED = {'UI': ('TigerBrowser2', 'TigerWK2App'),
            'WEB': ('TigerWebProcess', 'TigerNetworkProcess'),
            'GPU': ('TigerGPUProcess',)}


def read_manifest(directory, process):
    directory = directory.resolve()
    data = json.loads((directory / 'build-manifest.json').read_text())
    if data.get('schema') != 1 or data.get('process') != process:
        raise ValueError('wrong manifest schema/process in ' + str(directory))
    source = data.get('source', {})
    for key in ('head', 'tree', 'dirty_sha256'):
        if not re.fullmatch(r'[0-9a-f]{40}|[0-9a-f]{64}', source.get(key, '')):
            raise ValueError('missing source identity ' + key + ' in ' + str(directory))
    if source['dirty_sha256'] != hashlib.sha256(b'').hexdigest():
        raise ValueError('release candidate must come from committed WebKit source: ' + str(directory))
    binaries = data.get('binaries', {})
    for name in REQUIRED[process]:
        if name not in binaries:
            raise ValueError('manifest lacks required executable: ' + name)
    for name, expected in binaries.items():
        path = Path(name)
        if path.is_absolute() or '..' in path.parts:
            raise ValueError('unsafe binary path in manifest: ' + name)
        binary = directory / 'bin' / path
        if not binary.is_file() or not os.access(binary, os.X_OK):
            raise ValueError('missing executable: ' + str(binary))
        if digest(binary) != expected:
            raise ValueError('binary hash differs from manifest: ' + str(binary))
    for kind in ('messages', 'serializers'):
        path = directory / ('wire-' + kind + '.txt')
        expected = data.get('wire', {}).get(kind + '_sha256')
        if not expected or not path.is_file() or not path.stat().st_size or digest(path) != expected:
            raise ValueError('missing or modified wire evidence: ' + str(path))
    return data


def verify(ui, web, gpu):
    manifests = [read_manifest(directory, process)
                 for process, directory in [('UI', ui), ('WEB', web), ('GPU', gpu)]]
    reference = manifests[0]
    for data in manifests[1:]:
        if data['source'] != reference['source']:
            raise ValueError('mixed WebKit source revisions in candidate')
        if data['wire'] != reference['wire']:
            raise ValueError('IPC message/serializer mismatch in candidate')
    return {'source': reference['source'], 'wire': reference['wire'],
            'processes': {data['process']: data['binaries'] for data in manifests}}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest='command', required=True)
    fp = commands.add_parser('fingerprint')
    fp.add_argument('--build-dir', type=Path, required=True)
    fp.add_argument('--out', type=Path, required=True)
    vp = commands.add_parser('verify')
    for name in ('ui', 'web', 'gpu'):
        vp.add_argument('--' + name, type=Path, required=True)
    args = parser.parse_args()
    try:
        result = fingerprint(args.build_dir, args.out) if args.command == 'fingerprint' else verify(args.ui, args.web, args.gpu)
        print(json.dumps(result, sort_keys=True))
    except (ValueError, OSError, KeyError, json.JSONDecodeError) as error:
        print('tiger-artifacts: ' + str(error), file=sys.stderr)
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
