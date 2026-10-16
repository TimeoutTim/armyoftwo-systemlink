# SPDX-License-Identifier: GPL-3.0-only
import json
from pathlib import Path
import shutil
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools'))
from apply_xex_delta import apply
from build_xex_delta import build


@unittest.skipUnless(shutil.which('xdelta3'), 'Requires xdelta3')
class DeltaTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.source = self.root/'source.xex'
        self.target = self.root/'target.xex'
        self.source.write_bytes(bytes(range(256))*100)
        self.target.write_bytes(self.source.read_bytes()[:9000]+b'LAN TEST'+self.source.read_bytes()[9007:])
        self.bundle = self.root/'bundle'
        build(self.source, self.target, self.bundle, 'Synthetic test', require_original=False)
        self.patch = self.bundle/'army-of-two.xdelta'
        self.manifest = self.bundle/'manifest.json'
        self.output = self.root/'result.xex'

    def test_exact_reconstruction_and_source_preserved(self):
        before = self.source.read_bytes()
        apply(self.source, self.patch, self.manifest, self.output)
        self.assertEqual(self.output.read_bytes(), self.target.read_bytes())
        self.assertEqual(self.source.read_bytes(), before)

    def test_wrong_source_rejected(self):
        self.source.write_bytes(b'wrong version')
        with self.assertRaises(ValueError):
            apply(self.source, self.patch, self.manifest, self.output)
        self.assertFalse(self.output.exists())

    def test_corrupt_patch_rejected(self):
        self.patch.write_bytes(self.patch.read_bytes()+b'bad')
        with self.assertRaises(ValueError):
            apply(self.source, self.patch, self.manifest, self.output)
        self.assertFalse(self.output.exists())

    def test_existing_output_preserved(self):
        self.output.write_bytes(b'keep')
        with self.assertRaises(FileExistsError):
            apply(self.source, self.patch, self.manifest, self.output)
        self.assertEqual(self.output.read_bytes(), b'keep')

    def test_wrong_target_digest_never_published(self):
        data = json.loads(self.manifest.read_text())
        data['target']['sha256'] = '0'*64
        self.manifest.write_text(json.dumps(data))
        with self.assertRaises(ValueError):
            apply(self.source, self.patch, self.manifest, self.output)
        self.assertFalse(self.output.exists())


if __name__ == '__main__':
    unittest.main()
