"""Exercise builder receipts with inert Windows/Linux freezer outputs."""
from pathlib import Path
import hashlib
import json
import runpy
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
SCRIPT = ROOT / 'tools/build_horizon_engine_host.py'


class EngineHostBuilderTests(unittest.TestCase):
    def build(self, platform, output, create=True):
        payload = b'inert freezer output; no executable code'

        def freeze(command, **kwargs):
            self.assertEqual(command[1:3], ['-m', 'PyInstaller'])
            self.assertTrue(kwargs['check'])
            if create:
                folder = Path(command[command.index('--distpath') + 1]) / 'horizon-engine-host'
                folder.mkdir(parents=True)
                name = 'horizon-engine-host.exe' if platform == 'win32' else 'horizon-engine-host'
                (folder / name).write_bytes(payload)

        with patch.object(sys, 'argv', [str(SCRIPT), '--output', str(output)]), \
             patch.object(sys, 'platform', platform), patch('subprocess.run', side_effect=freeze):
            runpy.run_path(str(SCRIPT), run_name='__main__')
        return payload

    def test_platform_outputs_produce_bound_provenance(self):
        for platform in ('win32', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as folder:
                output = Path(folder)
                payload = self.build(platform, output)
                raw = (output / 'ENGINE-HOST-SOURCE.json').read_bytes()
                receipt = json.loads(raw)
                self.assertEqual(receipt['component_version'],json.loads((ROOT/'tools/component_versions.json').read_bytes())['gpu'])
                self.assertEqual(receipt['executable_sha256'], hashlib.sha256(payload).hexdigest())
                self.assertEqual(receipt['sources']['tools/build_horizon_engine_host.py'],
                                 hashlib.sha256(SCRIPT.read_bytes()).hexdigest())
                self.assertEqual((output / 'ENGINE-HOST-SOURCE.sha256').read_text().strip(),
                                 hashlib.sha256(raw).hexdigest())

    def test_missing_output_cannot_publish_a_receipt(self):
        for platform in ('win32', 'linux'):
            with self.subTest(platform=platform), tempfile.TemporaryDirectory() as folder:
                output = Path(folder)
                with self.assertRaises(FileNotFoundError):
                    self.build(platform, output, create=False)
                self.assertFalse((output / 'ENGINE-HOST-SOURCE.json').exists())


if __name__ == '__main__':
    unittest.main()
