"""Actual DPAPI role-key protection and reference-TUF client acceptance.

Everything is temporary. No production publisher key, application process,
wallet, remote request or GPU is used.
"""
from pathlib import Path
import json,os,sys,unittest
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import publish_update_metadata as publisher
from horizon_update_client import AuthenticatedUpdates,digest
from test_authenticated_updates import Transport
import test_component_install as fixtures

@unittest.skipUnless(os.name=='nt','Windows publisher DPAPI')
class PublisherTests(unittest.TestCase):
 def setUp(self):
  self.fixture=fixtures.InstallTests();self.fixture.setUp();self.root=self.fixture.root;self.keys=self.root/'publisher-private';self.bootstrap=publisher.initialize_keys(self.keys,'testnet','5'*64);self.archive=self.root/'update.zip';self.receipt=dict(manifest=self.fixture.manifest,bytes=self.archive.stat().st_size,archive_sha256=digest(self.archive.read_bytes()))
 def tearDown(self):self.fixture.tearDown()
 def test_distinct_role_keys_encrypted_at_rest_and_existing_keys_never_overwritten(self):
  store=json.loads((self.keys/'key-store.json').read_bytes());self.assertEqual(len(set(store['roles'].values())),4)
  for role in publisher.ROLES:
   encrypted=(self.keys/(role+'.dpapi')).read_bytes();self.assertNotIn(b'PRIVATE KEY',encrypted)
   signer,_=publisher._signer(self.keys,role);self.assertEqual(signer.public_key.keyid,store['roles'][role])
  with self.assertRaises(ValueError):publisher.initialize_keys(self.keys,'testnet','5'*64)
 def test_protected_key_cannot_be_opened_with_another_role_purpose(self):
  raw=(self.keys/'root.dpapi').read_bytes()
  with self.assertRaises(OSError):publisher.protected(raw,b'ANOTHER PURPOSE',True)
 def test_published_consistent_snapshot_downloads_and_stages_with_real_tuf_verification(self):
  output=self.root/'public';result=publisher.publish(self.keys,self.archive,self.receipt,output,2);self.assertFalse(result['uploaded']);self.assertFalse(result['private_key_files_exported'])
  metadata='https://updates.invalid/metadata/';targets='https://updates.invalid/targets/';transport=Transport()
  for file in (output/'metadata').iterdir():transport.files[metadata+file.name]=file.read_bytes()
  for file in (output/'targets').iterdir():transport.files[targets+file.name]=file.read_bytes()
  client=AuthenticatedUpdates(self.root/'update-client',self.bootstrap,digest(self.bootstrap),metadata,targets,self.fixture.context,self.fixture.context_sha,fetcher=transport)
  prepared=client.prepare(['cpu']);self.assertTrue(prepared['publisher_authenticated']);self.assertTrue((Path(prepared['stage'])/'payload/cpu/EngineRuntime/cpu/horizon-cpu-controller.exe').is_file())
  candidate=self.fixture.store.prepare(client,['cpu'])
  self.assertTrue(self.fixture.store.activate(candidate,lambda:True,lambda path:True,lambda *args:self.fail('Unexpected rollback')))
  self.assertEqual(self.fixture.store.current()['cohort'],candidate['cohort'])
  self.assertEqual(len(list(output.rglob('*.dpapi'))),0)
 def test_receipt_mismatch_is_refused_before_any_public_output(self):
  self.receipt['manifest']['components'][0]['version']='9.0.0'
  with self.assertRaisesRegex(ValueError,'another archive'):publisher.publish(self.keys,self.archive,self.receipt,self.root/'public',2)
  self.assertFalse((self.root/'public').exists())
 def test_foreign_network_refused_before_any_public_output(self):
  self.receipt['manifest']['genesis_hash']='6'*64
  with self.assertRaisesRegex(ValueError,'chain differs'):publisher.publish(self.keys,self.archive,self.receipt,self.root/'public',2)
  self.assertFalse((self.root/'public').exists())
 def test_signed_metadata_version_cannot_be_reused_or_decreased(self):
  publisher.publish(self.keys,self.archive,self.receipt,self.root/'first',2)
  for version in (2,1):
   with self.assertRaisesRegex(ValueError,'already reserved'):publisher.publish(self.keys,self.archive,self.receipt,self.root/('reused-'+str(version)),version)
  result=publisher.publish(self.keys,self.archive,self.receipt,self.root/'renewed',3)
  self.assertEqual(result['metadata_version'],3);self.assertEqual(json.loads((self.keys/'publication-state.json').read_bytes())['state'],'prepared')
if __name__=='__main__':unittest.main()
