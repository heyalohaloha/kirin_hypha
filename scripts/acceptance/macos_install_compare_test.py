"""Pure synthetic fixtures; no real bundles or attributes are modified."""
import os
from pathlib import Path
import tempfile
import unittest
from macos_install_compare import inventory, xattrs_without_macos_provenance as normalize

ATTR = b'com.example.test:\n00000000  61 62 63                                         |abc|\n'
PROV = b'com.apple.provenance:\n00000000  01 02                                            |..|\n'


class Comparison(unittest.TestCase):
    def test_only_exact_tracking_name_is_excluded(self):
        self.assertEqual(normalize(b''), b'')
        self.assertEqual(normalize(ATTR), ATTR)
        self.assertEqual(normalize(PROV), b'')
        self.assertEqual(normalize(PROV + ATTR), ATTR)
        self.assertEqual(normalize(ATTR + PROV), ATTR)
        near = PROV.replace(b'provenance:', b'provenance.extra:')
        self.assertEqual(normalize(near), near)
        quarantine = ATTR.replace(b'com.example.test', b'com.apple.quarantine')
        self.assertEqual(normalize(quarantine + PROV), quarantine)

    def test_unknown_output_fails_closed(self):
        for bad in (b'bad', b'00000000  FF\n', b':\n', b'name:\nnot hex\n'):
            with self.assertRaises(ValueError):
                normalize(bad)

    def test_install_comparison_keeps_content_mode_links_and_other_attributes(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp).resolve()
            a, b = root / 'payload', root / 'installed'
            for directory in (a, b):
                directory.mkdir()
                (directory / 'file').write_bytes(b'payload')
                (directory / 'link').symlink_to('file')
            baseline = inventory(a, lambda _: ATTR)
            self.assertEqual(baseline, inventory(b, lambda _: ATTR + PROV))
            self.assertNotEqual(baseline, inventory(b, lambda _: ATTR.replace(b'61', b'62')))
            (b / 'file').write_bytes(b'changed')
            self.assertNotEqual(baseline, inventory(b, lambda _: ATTR))
            (b / 'file').write_bytes(b'payload')
            os.chmod(b / 'file', 0o700)
            self.assertNotEqual(baseline, inventory(b, lambda _: ATTR))
            (b / 'link').unlink()
            (b / 'link').symlink_to('elsewhere')
            self.assertNotEqual(baseline, inventory(b, lambda _: ATTR))
            with self.assertRaises(ValueError):
                inventory(b / 'link', lambda _: b'')

    def test_changing_attributes_are_not_ignored(self):
        with tempfile.TemporaryDirectory() as tmp:
            calls = []
            def changing(_):
                calls.append(1)
                return b'' if len(calls) == 1 else PROV
            with self.assertRaises(ValueError):
                inventory(Path(tmp).resolve(), changing)


if __name__ == '__main__':
    unittest.main()
