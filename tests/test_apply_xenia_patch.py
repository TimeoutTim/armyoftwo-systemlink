# SPDX-License-Identifier: GPL-3.0-only
import hashlib
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from apply_xenia_patch import apply


class PatchTests(unittest.TestCase):
    def setUp(self):
        self.image = bytes(32)
        self.doc = {'title_id': 'TEST', 'hash': 'HASH', 'patch': [
            {'name': 'test', 'is_enabled': True,
             'be32': [{'address': 0x1004, 'value': 0x12345678}]}]}
        self.guards = {'title_id': 'TEST', 'module_hash': 'HASH', 'image_base': 0x1000,
            'source_image_sha256': hashlib.sha256(self.image).hexdigest(),
            'writes': [{'address': 0x1004, 'before': '00000000'}]}

    def test_big_endian_exact_edit(self):
        result, report = apply(self.image, self.doc, self.guards)
        self.assertEqual(result, bytes(4)+bytes.fromhex('12345678')+bytes(24))
        self.assertEqual(len(report['edits']), 1)

    def test_disabled_requires_explicit_selection(self):
        self.doc['patch'][0]['is_enabled'] = False
        with self.assertRaises(ValueError): apply(self.image, self.doc, self.guards)
        self.assertEqual(apply(self.image, self.doc, self.guards, ['test'])[0][4:8],
                         bytes.fromhex('12345678'))

    def test_unknown_operation_rejected(self):
        self.doc['patch'][0]['mystery'] = []
        with self.assertRaises(ValueError): apply(self.image, self.doc, self.guards)

    def test_guard_required(self):
        self.guards['writes'] = []
        with self.assertRaises(ValueError): apply(self.image, self.doc, self.guards)

    def test_overlap_rejected(self):
        self.doc['patch'][0]['be32'] *= 2
        with self.assertRaises(ValueError): apply(self.image, self.doc, self.guards)

    def test_wrong_image_rejected(self):
        with self.assertRaises(ValueError): apply(b'x'+self.image[1:], self.doc, self.guards)

    def test_array_exact_bytes(self):
        entry = self.doc['patch'][0]
        entry['array'] = [{'address': 0x1004, 'value': '0x11223344'}]
        del entry['be32']
        self.assertEqual(apply(self.image, self.doc, self.guards)[0][4:8], bytes.fromhex('11223344'))


if __name__ == '__main__':
    unittest.main()
