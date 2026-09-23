import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location('artifacts', Path(__file__).resolve().parents[2] / 'tools/tiger-artifacts.py')
artifacts = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(artifacts)


class ArtifactTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.paths = {}
        for process, names in artifacts.REQUIRED.items():
            directory = Path(self.temp.name) / process
            (directory / 'bin').mkdir(parents=True)
            binaries = {}
            for name in names:
                binary = directory / 'bin' / name
                binary.write_bytes((process + name).encode())
                binary.chmod(0o755)
                binaries[name] = artifacts.digest(binary)
            wire = {}
            for kind in ('messages', 'serializers'):
                path = directory / ('wire-' + kind + '.txt')
                path.write_text(kind + '\n')
                wire[kind + '_sha256'] = artifacts.digest(path)
            manifest = {'schema': 1, 'process': process,
                        'source': {'head': 'a' * 40, 'tree': 'b' * 40,
                                   'dirty_sha256': hashlib.sha256(b'').hexdigest()},
                        'binaries': binaries, 'wire': wire}
            (directory / 'build-manifest.json').write_text(json.dumps(manifest))
            self.paths[process] = directory

    def verify(self):
        return artifacts.verify(self.paths['UI'], self.paths['WEB'], self.paths['GPU'])

    def alter(self, process, change):
        path = self.paths[process] / 'build-manifest.json'
        data = json.loads(path.read_text())
        change(data)
        path.write_text(json.dumps(data))

    def test_matched_bundle(self):
        self.assertEqual(self.verify()['source']['head'], 'a' * 40)

    def test_failed_build_leaving_old_binary_cannot_match_new_manifest(self):
        (self.paths['WEB'] / 'bin/TigerWebProcess').write_bytes(b'old executable')
        with self.assertRaisesRegex(ValueError, 'binary hash'):
            self.verify()

    def test_missing_helper(self):
        (self.paths['GPU'] / 'bin/TigerGPUProcess').unlink()
        with self.assertRaisesRegex(ValueError, 'missing executable'):
            self.verify()

    def test_different_source_revision(self):
        self.alter('GPU', lambda d: d['source'].update(head='c' * 40))
        with self.assertRaisesRegex(ValueError, 'mixed WebKit'):
            self.verify()

    def test_changed_serializer_with_valid_hash_is_rejected(self):
        path = self.paths['GPU'] / 'wire-serializers.txt'
        path.write_text('different members\n')
        self.alter('GPU', lambda d: d['wire'].update(serializers_sha256=artifacts.digest(path)))
        with self.assertRaisesRegex(ValueError, 'serializer mismatch'):
            self.verify()

    def test_modified_wire_evidence(self):
        (self.paths['WEB'] / 'wire-messages.txt').write_text('')
        with self.assertRaisesRegex(ValueError, 'wire evidence'):
            self.verify()

    def test_dirty_source_not_a_release_candidate(self):
        self.alter('UI', lambda d: d['source'].update(dirty_sha256='d' * 64))
        with self.assertRaisesRegex(ValueError, 'committed WebKit'):
            self.verify()

    def test_manifest_cannot_escape_binary_directory(self):
        self.alter('UI', lambda d: d['binaries'].update({'../elsewhere': 'e' * 64}))
        with self.assertRaisesRegex(ValueError, 'unsafe binary path'):
            self.verify()

    def test_preprocessing_does_not_write_object_or_dependency_files(self):
        command, source = artifacts.preprocess_command(
            'wrapper ccache clang++ -DTEST=1 -Iinclude -MD -MT x.o -MF x.d '
            '-Xclang -include-pch -Xclang prefix.pch -o x.o -c source.cpp')
        self.assertEqual(command, ['wrapper', 'ccache', 'clang++', '-DTEST=1', '-Iinclude', '-E'])
        self.assertEqual(source, 'source.cpp')

    def test_unexpected_shell_command_is_refused(self):
        with self.assertRaisesRegex(ValueError, 'shell operator'):
            artifacts.preprocess_command('clang++ -c a.cpp -o a.o && touch marker')

    def test_message_preprocessing_preserves_aliases_and_ignores_line_markers(self):
        names = artifacts.message_names('''enum class MessageName : uint16_t {
            First = 0,
# 301 "/different/build/MessageNames.h"
            DrawingArea_Update,
            FirstSynchronous,
            LastAsynchronous = FirstSynchronous - 1,
        };''')
        self.assertEqual(names, ['First = 0', 'DrawingArea_Update', 'FirstSynchronous',
                                'LastAsynchronous = FirstSynchronous - 1'])


if __name__ == '__main__':
    unittest.main()
