"""Repeated signed updates in a small cache; no production data or keys."""
from copy import deepcopy
from pathlib import Path
from unittest.mock import patch
import json, os, subprocess, sys, unittest
P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'tools'), str(P/'src/update')]
import horizon_update_client as client_module
import test_authenticated_updates as fixtures
import stage_component_bundle as stage
from tuf.api.exceptions import BadVersionNumberError
from tuf.api.metadata import Metadata, Root
from tuf.api.serialization.json import JSONSerializer


class CacheLifetimeTests(unittest.TestCase):
    setUp = fixtures.UpdateTests.setUp
    tearDown = fixtures.UpdateTests.tearDown
    raw = fixtures.UpdateTests.raw
    publish = fixtures.UpdateTests.publish
    client = fixtures.UpdateTests.client

    def size(self, cache):
        return sum(p.stat().st_size for p in cache.rglob('*') if p.is_file())

    def test_full_existing_cache_recycles_downloads_before_metadata_refresh(self):
        client = self.client(); client.refresh(['cpu'])
        metadata = {p: p.read_bytes() for p in client.metadata.rglob('*') if p.is_file()}
        unknown = client.cache / 'do-not-delete.txt'; unknown.write_bytes(b'UNRECOGNIZED')
        limit = self.size(client.cache) + 1024
        old = client.targets / ('a'*64 + '-old-bundle.zip'); old.write_bytes(b'x'*8192)
        with patch.object(client_module, 'MAX_CACHE', limit):
            self.assertTrue(client.refresh(['cpu'])['publisher_authenticated'])
        self.assertFalse(old.exists())
        self.assertEqual(unknown.read_bytes(), b'UNRECOGNIZED')
        for p, data in metadata.items(): self.assertEqual(p.read_bytes(), data)

    def test_retention_target_cannot_disable_an_authenticated_update(self):
        client = self.client()
        unknown = client.cache/'unrecognized-user-file'; unknown.write_bytes(b'KEEP')
        # Even the trust metadata and this transaction exceed the tiny target.
        # It is an eviction target, not permission to delete history or a cap
        # on the lifetime of the installation.
        with patch.object(client_module, 'MAX_CACHE', 1):
            receipt = client.prepare(['cpu'])
        self.assertTrue(receipt['publisher_authenticated'])
        self.assertEqual((Path(receipt['stage'])/'payload/cpu/controller.exe').read_bytes(), self.pe)
        self.assertEqual(unknown.read_bytes(), b'KEEP')
        self.assertGreater(client.cache_budget_pressure['retained_bytes'], 1)

    def test_verified_consumer_releases_temporary_copies_immediately(self):
        client = self.client(); client.refresh(['cpu'])
        history = {p:p.read_bytes() for p in client.metadata.rglob('*') if p.is_file()}
        independent = self.root/'verified-independent-code'
        with client.prepared(['cpu'], discard_after=True) as receipt:
            stage_path = Path(receipt['stage'])
            independent.write_bytes((stage_path/'payload/cpu/controller.exe').read_bytes())
            self.assertTrue(stage_path.is_dir())
        self.assertFalse(stage_path.exists())
        self.assertFalse(list(client.targets.iterdir()))
        self.assertFalse(list((client.cache/'receipts').glob('*.json')))
        self.assertEqual(independent.read_bytes(), self.pe)
        for p, raw in history.items(): self.assertEqual(p.read_bytes(), raw)

    def test_first_check_retires_old_helper_leftovers_even_below_budget(self):
        client = self.client(); receipt = client.prepare(['cpu'])
        self.assertLess(self.size(client.cache), client_module.MAX_CACHE)
        history = {p:p.read_bytes() for p in client.metadata.rglob('*') if p.is_file()}
        view = client.refresh(['cpu'])
        self.assertTrue(view['publisher_authenticated']); self.assertEqual(view['warning'], '')
        self.assertFalse(Path(receipt['stage']).exists())
        self.assertFalse(list(client.targets.glob('*.zip')))
        self.assertEqual(len(list(client.targets.glob('*-components.json'))), 1)
        for p, raw in history.items(): self.assertEqual(p.read_bytes(), raw)

    def test_failed_consumer_retains_its_authenticated_stage_for_retry(self):
        client = self.client()
        with self.assertRaisesRegex(ValueError, 'Consumer failed'):
            with client.prepared(['cpu'], discard_after=True) as receipt:
                stage_path = Path(receipt['stage'])
                raise ValueError('Consumer failed before independent verification')
        self.assertTrue(stage_path.is_dir())
        self.assertTrue(list(client.targets.glob('*-components.zip')))

    def test_cleanup_failure_cannot_invalidate_verified_code(self):
        client = self.client(); independent = self.root/'independent-code'
        with patch.object(client, '_discard_stage', side_effect=PermissionError('injected cleanup denial')):
            with client.prepared(['cpu'], discard_after=True) as receipt:
                independent.write_bytes((Path(receipt['stage'])/'payload/cpu/controller.exe').read_bytes())
        self.assertEqual(independent.read_bytes(), self.pe)
        self.assertIn('could not be cleaned', client.cache_cleanup_warning)

    def test_many_versions_do_not_accumulate_unbounded_stages(self):
        client = self.client(); result = client.prepare(['cpu'])
        limit = self.size(client.cache) + 8192
        with patch.object(client_module, 'MAX_CACHE', limit), patch.object(client_module, 'MAX_CATALOG', 4096):
            for version in range(2, 22):
                self.manifest['components'][0]['version'] = f'1.0.{version}'
                self.publish(version=version)
                result = client.prepare(['cpu'])
                self.assertEqual((Path(result['stage'])/'payload/cpu/controller.exe').read_bytes(), self.pe)
                self.assertLessEqual(self.size(client.cache), limit)

    def test_changed_context_reclaims_previous_stage_without_redownload(self):
        client = self.client(); first = client.prepare(['cpu'])
        archive = next(client.targets.glob('*-components.zip'))
        before = archive.read_bytes(); limit = self.size(client.cache) + 8192
        context = deepcopy(self.context); extra = self.root/'another-data-root'; extra.mkdir()
        context['protected_roots'].append(str(extra))
        cf = self.root/'context2.json'; cf.write_text(json.dumps(context))
        context, context_sha = stage.read_context(cf)
        second = self.client(context); second.context_sha256 = context_sha
        with patch.object(client_module, 'MAX_CACHE', limit), patch.object(client_module, 'MAX_CATALOG', 4096):
            result = second.prepare(['cpu'])
        self.assertTrue(result['publisher_authenticated'])
        self.assertNotEqual(result['stage'], first['stage'])
        self.assertEqual(archive.read_bytes(), before)
        self.assertFalse(Path(first['stage']).exists())

    def test_stage_is_locked_for_its_entire_consumption(self):
        client = self.client()
        with client.prepared(['cpu']) as receipt:
            self.assertTrue(Path(receipt['stage']).is_dir())
            with self.assertRaises(OSError): self.client().refresh(['cpu'])
        self.assertTrue(self.client().refresh(['cpu'])['publisher_authenticated'])

    def test_separate_helper_process_cannot_collect_a_consumed_stage(self):
        client = self.client()
        probe = """import sys
sys.path.insert(0,sys.argv[1])
from stage_component_bundle import owned_lock
try:
 with owned_lock(sys.argv[2]): pass
except OSError: sys.exit(16)
"""
        with client.prepared(['cpu']) as receipt:
            result = subprocess.run([sys.executable, '-I', '-c', probe, str(P/'tools'), str(client.cache)],
                                    capture_output=True, timeout=15)
            self.assertEqual(result.returncode, 16, result.stderr.decode(errors='replace'))
            self.assertTrue(Path(receipt['stage']).is_dir())

    def test_new_build_root_recycles_old_targets_but_keeps_all_trust_history(self):
        old = self.client(); old.prepare(['cpu'])
        history = {p:p.read_bytes() for p in old.metadata.rglob('*') if p.is_file()}
        root = Metadata(Root(version=2, expires=self.expires, consistent_snapshot=False))
        for role, signer in self.signers.items(): root.signed.add_key(signer.public_key, role)
        root.sign(self.signers['root']); bootstrap = root.to_bytes(JSONSerializer(compact=True))
        new = client_module.AuthenticatedUpdates(old.cache, bootstrap, client_module.digest(bootstrap),
                 self.base, self.target, self.context, self.context_sha, fetcher=self.transport)
        with patch.object(client_module, 'MAX_CACHE', 1):
            result = new.prepare(['cpu'])
        self.assertTrue(result['publisher_authenticated'])
        self.assertNotEqual(new.metadata, old.metadata)
        for p, data in history.items(): self.assertEqual(p.read_bytes(), data)
        self.assertFalse(list(old.targets.iterdir()))

    def test_recycling_never_resets_tuf_rollback_history(self):
        client = self.client(); self.publish(version=2); client.refresh(['cpu'])
        limit = self.size(client.cache) + 100
        (client.targets/('c'*64+'-old.zip')).write_bytes(b'x'*8192)
        with patch.object(client_module, 'MAX_CACHE', limit): client.refresh(['cpu'])
        self.publish(version=1)
        with self.assertRaises(BadVersionNumberError): client.refresh(['cpu'])

    def test_interrupted_extraction_without_receipt_is_rebuilt(self):
        client = self.client(); first = client.prepare(['cpu'])
        for receipt in (client.cache/'receipts').glob('*.json'): receipt.unlink()
        second = client.prepare(['cpu'])
        self.assertEqual(second, first)
        self.assertEqual((Path(second['stage'])/'payload/cpu/controller.exe').read_bytes(), self.pe)

    def test_interrupted_gc_and_download_are_reclaimed_under_pressure(self):
        client = self.client(); client.refresh(['cpu']); limit = self.size(client.cache)+100
        gc = client.cache/('.pending-gc-'+'d'*32); gc.mkdir(); (gc/'partial.dll').write_bytes(b'x'*8192)
        download = client.targets/('.pending-download-'+'e'*32); download.write_bytes(b'y'*8192)
        with patch.object(client_module, 'MAX_CACHE', limit): client.refresh(['cpu'])
        self.assertFalse(gc.exists()); self.assertFalse(download.exists())

    def test_linked_stage_is_refused_before_any_reclamation(self):
        client = self.client(); receipt = client.prepare(['cpu'])
        link = Path(receipt['stage'])/'linked-private-data'
        try: os.symlink(self.sentinel, link)
        except OSError: self.skipTest('Symlink privilege unavailable')
        obsolete = client.targets/('b'*64+'-old.zip'); obsolete.write_bytes(b'x'*8192)
        with patch.object(client_module, 'MAX_CACHE', 100):
            with self.assertRaisesRegex(stage.StageError, 'Linked'): client.refresh(['cpu'])
        self.assertTrue(obsolete.exists())
        self.assertTrue(Path(receipt['stage']).exists())


if __name__ == '__main__': unittest.main()
