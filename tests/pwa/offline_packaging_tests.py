"""The offline package must be complete, reproducible and tied to canonical lessons."""
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location('package_pwa', ROOT / 'scripts/package_pwa.py')
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


class OfflinePackageTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.out = Path(self.temp.name) / 'app'
        self.out.mkdir()
        for name in package.CORE_FILES:
            (self.out / name).write_text('fixture: ' + name)

    def build(self):
        return package.build(self.out, ROOT / 'docs/help/topics.json', ROOT / 'website-preview/dist/velocity-corporate-favicon.png')

    def test_complete_manifest_and_canonical_help(self):
        self.build()
        manifest = json.loads((self.out / 'manifest.webmanifest').read_text())
        self.assertEqual([manifest[k] for k in ('id', 'scope', 'start_url')], ['./'] * 3)
        from PIL import Image
        for icon in manifest['icons']:
            with Image.open(self.out / icon['src']) as picture:
                self.assertEqual(icon['sizes'], f'{picture.width}x{picture.height}')
        canonical = json.loads((ROOT / 'docs/help/topics.json').read_text(encoding='utf-8'))
        help_data = json.loads((self.out / 'help.json').read_text(encoding='utf-8'))
        self.assertGreater(len(help_data['topics']), 10)
        self.assertTrue(all(topic in canonical['topics'] for topic in help_data['topics']))

    def test_missing_asset_rejects_package(self):
        (self.out / 'engine.wasm').unlink()
        with self.assertRaises(FileNotFoundError):
            self.build()
        self.assertFalse((self.out / 'sw.js').exists())

    def test_deterministic_version_changes_with_shell_contents(self):
        first = self.build()
        worker = (self.out / 'sw.js').read_bytes()
        self.assertEqual(first, self.build())
        self.assertEqual(worker, (self.out / 'sw.js').read_bytes())
        (self.out / 'app.mjs').write_text('changed')
        self.assertNotEqual(first, self.build())


if __name__ == '__main__':
    unittest.main()
