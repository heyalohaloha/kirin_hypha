import os
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import aax_swap_source


@unittest.skipIf(os.name == 'nt', 'descriptor-relative POSIX APIs required')
class SourceDescriptorTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='aax-source-')
        self.root = Path(os.path.realpath(self.tmp.name))
        self.bundle = self.root / 'bundle'
        (self.bundle / 'Contents').mkdir(parents=True)
        (self.bundle / 'Contents' / 'payload').write_bytes(b'candidate')

    def tearDown(self):
        self.tmp.cleanup()

    def test_snapshot_and_hash_match(self):
        inspection = aax_swap_source.inspect_source(str(self.bundle), include_bytes=True)
        self.assertEqual(inspection['files'][0]['path'], 'Contents/payload')
        self.assertEqual(inspection['files'][0]['base64'], 'Y2FuZGlkYXRl')
        self.assertEqual(inspection['sha256'], aax_swap_source.inspect_source(str(self.bundle))['sha256'])

    @unittest.skipUnless(sys.platform == 'darwin', 'Darwin xattr fixture')
    def test_snapshot_records_extended_attributes(self):
        result = subprocess.run(['/usr/bin/xattr', '-w', 'com.example.fixture', 'test', str(self.bundle / 'Contents' / 'payload')], capture_output=True, text=True)
        if result.returncode:
            self.skipTest('fixture xattr unavailable: ' + result.stderr.strip())
        inspection = aax_swap_source.inspect_source(str(self.bundle), include_bytes=True)
        item = next(record for record in inspection['attributes'] if record['path'] == 'Contents/payload')
        self.assertIn({'name': 'com.example.fixture', 'base64': 'dGVzdA=='}, item['values'])
        self.assertRegex(inspection['metadataSha256'], r'^[a-f0-9]{64}$')

    @unittest.skipUnless(sys.platform == 'darwin', 'Darwin ACL fixture')
    def test_snapshot_refuses_extended_acl_instead_of_dropping_it(self):
        result = subprocess.run(['/bin/chmod', '+a', 'everyone allow read', str(self.bundle / 'Contents' / 'payload')], capture_output=True, text=True)
        if result.returncode:
            self.skipTest('fixture ACL unavailable: ' + result.stderr.strip())
        with self.assertRaisesRegex(ValueError, 'ACL'):
            aax_swap_source.inspect_source(str(self.bundle), include_bytes=True)

    def test_symlink_replacement_before_open_is_never_followed(self):
        outside = self.root / 'outside'
        outside.mkdir()
        (outside / 'payload').write_bytes(b'outside')
        original_open = os.open
        inserted = False
        outside_opened = False

        def replace_then_open(name, flags, *args, **kwargs):
            nonlocal inserted, outside_opened
            if name == 'Contents' and flags & os.O_DIRECTORY and not inserted:
                inserted = True
                os.rename(self.bundle / 'Contents', self.bundle / 'saved')
                os.symlink(outside, self.bundle / 'Contents')
            if inserted and name == 'payload':
                outside_opened = True
            return original_open(name, flags, *args, **kwargs)

        with mock.patch.object(aax_swap_source.os, 'open', side_effect=replace_then_open):
            with self.assertRaises((ValueError, OSError)):
                aax_swap_source.inspect_source(str(self.bundle), include_bytes=True)
        self.assertTrue(inserted)
        self.assertFalse(outside_opened)

    def test_original_symlinks_are_also_refused(self):
        os.symlink('payload', self.bundle / 'Contents' / 'signature-link')
        with self.assertRaisesRegex(ValueError, 'symlink'):
            aax_swap_source.inspect_source(str(self.bundle), allow_existing_links=True)

    def test_refuses_hardlinks_and_symlink_ancestors(self):
        os.link(self.bundle / 'Contents' / 'payload', self.root / 'hard')
        with self.assertRaisesRegex(ValueError, 'hardlink'):
            aax_swap_source.inspect_source(str(self.bundle))
        os.unlink(self.root / 'hard')
        os.symlink(self.bundle, self.root / 'alias')
        with self.assertRaises((ValueError, OSError)):
            aax_swap_source.inspect_source(str(self.root / 'alias'))


if __name__ == '__main__':
    unittest.main()
