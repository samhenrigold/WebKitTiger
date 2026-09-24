#!/usr/bin/env python3
"""Exercise the actual manifest container/table matcher with reordered faces.

Host tests provide public-ATS-like raw table bytes; they do not emulate ATS font
activation. The scoped Tiger regeneration supplies that independent live check.
"""
import importlib.util
import json
import os
from pathlib import Path
import shlex
import struct
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('font_manifest', ROOT / 'tools/regenerate-font-manifest.py')
manifest = importlib.util.module_from_spec(spec)
spec.loader.exec_module(manifest)


def sfnt(identity, units=2048, raw_name=b'Raw SFNT name differs from ATS alias'):
    head = bytearray(54)
    struct.pack_into('>H', head, 18, units)
    head[-1] = identity
    hhea = bytearray(36)
    struct.pack_into('>hhh', hhea, 4, 1000 + identity, -200 - identity, identity)
    tables = {b'head': bytes(head), b'hhea': bytes(hhea), b'maxp': struct.pack('>IH', 0x10000, 100 + identity),
              b'name': raw_name}
    directory = bytearray(struct.pack('>IHHHH', 0x10000, len(tables), 0, 0, 0))
    offset = 12 + len(tables) * 16
    payload = bytearray()
    for tag, data in sorted(tables.items()):
        directory.extend(struct.pack('>4sIII', tag, 0, offset + len(payload), len(data)))
        payload.extend(data)
        payload.extend(b'\0' * (-len(payload) % 4))
    return bytes(directory + payload)


def resource(faces):
    data = bytearray()
    offsets = []
    for face in faces:
        offsets.append(len(data))
        data.extend(struct.pack('>I', len(face)) + face)
    data_offset = 256
    map_offset = data_offset + len(data)
    resource_map = bytearray(28)
    struct.pack_into('>HH', resource_map, 24, 28, 38 + 12 * len(faces))
    resource_map.extend(struct.pack('>H4sHH', 0, b'sfnt', len(faces) - 1, 10))
    # Deliberately unsorted IDs: FreeType must preserve sfnt reference order.
    for index, offset in enumerate(offsets):
        resource_map.extend(struct.pack('>hHII', 300 - index, 0xFFFF, offset, 0))
    header = struct.pack('>IIII', data_offset, map_offset, len(data), len(resource_map))
    return header + b'\0' * (data_offset - len(header)) + bytes(data + resource_map)


def ttc(faces):
    header_size = 12 + 4 * len(faces)
    offsets = []
    payload = bytearray()
    for face in faces:
        offset = header_size + len(payload)
        offsets.append(offset)
        adjusted = bytearray(face)
        count = struct.unpack_from('>H', face, 4)[0]
        for table in range(count):
            record = 12 + table * 16
            relative = struct.unpack_from('>I', face, record + 8)[0]
            struct.pack_into('>I', adjusted, record + 8, relative + offset)
        payload.extend(adjusted)
    return struct.pack('>III', 0x74746366, 0x10000, len(faces)) + struct.pack('>' + 'I' * len(faces), *offsets) + payload


class TableIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory(prefix='font-manifest-test-')
        cls.addClassCleanup(cls.temp.cleanup)
        cls.directory = Path(cls.temp.name)
        source = (ROOT / 'spike/fontmanifest.c').read_text()
        production = source.split('/* ---- sfnt reading', 1)[1].split('/* ---- JSON', 1)[0]
        production = production.split('*/', 1)[1]
        harness = cls.directory / 'matcher.c'
        harness.write_text('#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n' + production + r'''
int main(int argc, char** argv) {
    struct container candidate, reference;
    struct identityTable tables[3];
    unsigned t;
    int index;
    if (argc != 3 || !readFile(argv[1], &candidate) || !readFile(argv[2], &reference)) {
        puts("invalid-container"); return 0;
    }
    struct sfnt ats = containerFace(&reference, 0);
    for (t = 0; t < 3; ++t) {
        size_t offset, length;
        if (!sfntTable(&ats, identityTags[t], &offset, &length)) {
            tables[t].bytes = NULL; tables[t].length = 0;
        } else {
            tables[t].bytes = reference.bytes + offset; tables[t].length = length;
        }
    }
    index = freeTypeIndexForTables(&candidate, tables);
    printf("index=%d", index);
    if (index >= 0) {
        struct sfnt matched = containerFace(&candidate, (unsigned)index);
        struct metrics metrics;
        sfntMetrics(&matched, &metrics);
        printf(" units=%u ascent=%d descent=%d", metrics.unitsPerEm, metrics.ascent, metrics.descent);
    }
    puts("");
    free(candidate.bytes); free(reference.bytes);
    return 0;
}
''')
        cls.binary = cls.directory / 'matcher'
        subprocess.run(shlex.split(os.environ.get('CC', 'cc')) + ['-std=c99', '-O1', '-g',
                       '-fsanitize=address,undefined', '-fno-omit-frame-pointer', str(harness), '-o', str(cls.binary)],
                       check=True, capture_output=True, text=True)

    def match(self, candidate, reference):
        path = self.directory / 'candidate.font'
        tables = self.directory / 'ats-tables.ttf'
        path.write_bytes(candidate)
        tables.write_bytes(reference)
        result = subprocess.run([str(self.binary), str(path), str(tables)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.strip()

    def test_resource_order_differs_from_native_and_metrics_follow_match(self):
        faces = [sfnt(index, units=2000 + index) for index in range(4)]
        candidate = resource(list(reversed(faces)))
        for native_index, face in enumerate(faces):
            with self.subTest(native_index=native_index):
                self.assertEqual(self.match(candidate, face),
                                 f'index={3 - native_index} units={2000 + native_index} ascent={1000 + native_index} descent={-200 - native_index}')

    def test_ttc_uses_absolute_table_offsets(self):
        faces = [sfnt(1), sfnt(2)]
        self.assertEqual(self.match(ttc(faces), faces[1]), 'index=1 units=2048 ascent=1002 descent=-202')

    def test_plain_font_does_not_depend_on_ats_name_table_rewriting(self):
        face = sfnt(7, raw_name=b'StoneSansSemITCTTSemi')
        ats = sfnt(7, raw_name=b'ATS expanded name table with aliases and extra records')
        self.assertEqual(self.match(face, ats), 'index=0 units=2048 ascent=1007 descent=-207')

    def test_resource_attributes_are_masked_before_offset_lookup(self):
        face = sfnt(4)
        candidate = bytearray(resource([face]))
        map_offset = struct.unpack_from('>I', candidate, 4)[0]
        candidate[map_offset + 38 + 4] = 0xE0
        self.assertEqual(self.match(bytes(candidate), face), 'index=0 units=2048 ascent=1004 descent=-204')

    def test_ambiguous_or_absent_identity_never_falls_back_to_zero(self):
        face = sfnt(1)
        self.assertEqual(self.match(resource([face, face]), face), 'index=-1')
        self.assertEqual(self.match(resource([face, sfnt(1, raw_name=b'different name')]), face), 'index=-1')
        self.assertEqual(self.match(resource([face]), sfnt(2)), 'index=-1')

    def test_every_identity_table_must_match(self):
        original = sfnt(2)
        for tag in (b'head', b'hhea', b'maxp'):
            modified = bytearray(original)
            count = struct.unpack_from('>H', modified, 4)[0]
            for i in range(count):
                record = 12 + i * 16
                if modified[record:record + 4] == tag:
                    offset = struct.unpack_from('>I', modified, record + 8)[0]
                    modified[offset] ^= 1
            with self.subTest(table=tag):
                self.assertEqual(self.match(resource([original]), bytes(modified)), 'index=-1')

    def test_truncated_and_cross_resource_tables_are_rejected(self):
        face = sfnt(3)
        for candidate in (ttc([face])[:14], resource([face])[:-5], b'\0' * 10):
            self.assertEqual(self.match(candidate, face), 'invalid-container')
        corrupt = bytearray(face)
        struct.pack_into('>I', corrupt, 12 + 8, len(face) + 20)
        self.assertEqual(self.match(resource([bytes(corrupt), face]), face), 'index=1 units=2048 ascent=1003 descent=-203')
        for cut in range(12):
            self.assertEqual(self.match(face[:cut], face), 'invalid-container')


class ManifestValidationTests(unittest.TestCase):
    def setUp(self):
        self.face = {'postScriptName': 'CourierNewPSMT', 'path': '/Library/Fonts/Courier New',
                     'faceIndex': 0, 'freeTypeIndex': 3, 'unitsPerEm': 2048}
        self.data = {'faces': [self.face], 'faceCount': 1, 'withHandle': 1,
                     'withFreeTypeIndex': 1, 'withoutFreeTypeIndex': 0, 'differentFaceIndices': 1}
        self.baseline = json.loads(json.dumps(self.data))
        self.log = 'self-check: 1 handles resolve to the named face, 0 do not\n'

    def test_reports_distinct_indices_and_preserved_native_handle(self):
        result = manifest.validate_manifest(self.data, self.baseline, self.log)
        self.assertEqual(result['indices_differ'], 1)
        self.assertEqual(result['native_baseline_handles_unchanged'], 1)

    def test_rejects_native_index_change(self):
        self.face['faceIndex'] = 3
        with self.assertRaisesRegex(ValueError, 'native handle changed'):
            manifest.validate_manifest(self.data, self.baseline, self.log)

    def test_rejects_missing_identity_incomplete_counts_and_failed_native_selfcheck(self):
        del self.face['freeTypeIndex']
        with self.assertRaisesRegex(ValueError, 'missing/invalid'):
            manifest.validate_manifest(self.data, self.baseline, self.log)
        self.face['freeTypeIndex'] = 3
        self.data['withFreeTypeIndex'] = 0
        with self.assertRaisesRegex(ValueError, 'resolution counts'):
            manifest.validate_manifest(self.data, self.baseline, self.log)
        self.data['withFreeTypeIndex'] = 1
        with self.assertRaisesRegex(ValueError, 'self-check'):
            manifest.validate_manifest(self.data, self.baseline, '')


if __name__ == '__main__':
    unittest.main()
