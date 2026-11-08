"""Real TUF signature/expiry/rollback checks with an in-memory transport.

Fixture PE bytes are never executed. No external server or production key is
used. The reference TUF client performs all cryptographic acceptance checks.
"""
from copy import deepcopy
from datetime import datetime,timedelta,timezone
import hashlib,io,json,os
from pathlib import Path
import sys,tempfile,unittest,zipfile
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
from securesystemslib.signer import CryptoSigner
from tuf.api.metadata import Metadata,Root,Targets,Snapshot,Timestamp,MetaFile,TargetFile
from tuf.api.serialization.json import JSONSerializer
from tuf.api.exceptions import DownloadHTTPError,UnsignedMetadataError,ExpiredMetadataError,BadVersionNumberError,DownloadLengthMismatchError
from tuf.ngclient.fetcher import FetcherInterface
import stage_component_bundle as stage
from horizon_update_client import AuthenticatedUpdates,digest,locked_cache

class Transport(FetcherInterface):
 def __init__(self):self.files={}
 def _fetch(self,url):
  if url not in self.files:raise DownloadHTTPError('Fixture target absent',404)
  yield self.files[url]

class UpdateTests(unittest.TestCase):
 def test_measured_progress_retains_authentication_and_exact_component_bytes(self):
  import horizon_update_progress as progress
  events=[];progress.configure(events.append)
  try:result=self.client().prepare(['cpu'])
  finally:progress.configure(None)
  self.assertTrue(result['publisher_authenticated'])
  self.assertTrue(any(e['stage']=='download'and e['total']>0 and e['completed']==e['total']for e in events))
  self.assertTrue(any(e['stage']=='verify'and e['component']=='cpu'and e['total']>0 and e['completed']==e['total']for e in events))
  self.assertTrue(all(set(e)=={'schema','stage','component','completed','total'}and 0<=e['completed']<=e['total']for e in events))
  self.assertTrue(self.sentinel.exists())
 def test_download_disk_full_is_explicit_and_preserves_installed_data(self):
  from types import SimpleNamespace
  from unittest.mock import patch
  client=self.client()
  with patch('shutil.disk_usage',return_value=SimpleNamespace(free=0)):
   with self.assertRaisesRegex(stage.StageError,'Insufficient download disk space'):client.prepare(['cpu'])
  self.assertEqual(self.sentinel.read_bytes(),b'PRIVATE SENTINEL NOT READ BY UPDATER')
  self.assertFalse(any(client.targets.glob('*-components.zip')))
 def test_same_volume_download_admits_both_copies_before_fetch(self):
  from types import SimpleNamespace
  from unittest.mock import patch
  client=self.client();updater=client._updater();updater.refresh();target=updater.get_targetinfo('components.zip')
  free=16*1024*1024+target.length+target.length//2
  with patch('shutil.disk_usage',return_value=SimpleNamespace(free=free)):
   with self.assertRaisesRegex(stage.StageError,'Insufficient download disk space'):
    client._download(updater,'components.zip',32*1024*1024)
  self.assertFalse(any(client.targets.glob('*-components.zip')))
 def test_failed_copy_never_publishes_a_partial_cache_target(self):
  from unittest.mock import patch
  import errno
  client=self.client();updater=client._updater();updater.refresh()
  def interrupted(target,filepath):
   Path(filepath).write_bytes(b'INTERRUPTED TRANSFER')
   raise OSError(errno.ENOSPC,'Injected destination-full failure')
  with patch.object(updater,'download_target',side_effect=interrupted):
   with self.assertRaisesRegex(stage.StageError,'Insufficient download disk space'):
    client._download(updater,'components.zip',32*1024*1024)
  self.assertFalse(any(client.targets.glob('*-components.zip')))
  self.assertFalse(any(client.targets.glob('.pending-*')))
  result=client.prepare(['cpu']);self.assertTrue(result['publisher_authenticated'])
 def test_permission_failure_retains_no_partial_download(self):
  from unittest.mock import patch
  import errno
  client=self.client();updater=client._updater();updater.refresh()
  def refused(target,filepath):
   Path(filepath).write_bytes(b'PARTIAL')
   raise PermissionError(errno.EACCES,'Injected permission failure')
  with patch.object(updater,'download_target',side_effect=refused):
   with self.assertRaisesRegex(stage.StageError,'could not be written'):
    client._download(updater,'components.zip',32*1024*1024)
  self.assertFalse(any(client.targets.glob('*-components.zip')))
  self.assertFalse(any(client.targets.glob('.pending-*')))
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.root=Path(self.tmp.name);self.install=self.root/'installation';self.install.mkdir();self.sentinel=self.install/'wallet.dat';self.sentinel.write_bytes(b'PRIVATE SENTINEL NOT READ BY UPDATER')
  c=dict(schema='PGN_COMPONENT_STAGE_CONTEXT_V1',network='testnet',genesis_hash='5'*64,platform='windows-x86_64',compatibility_set='qa.cohort',minimum_sequence=1,installed=[],protected_roots=[str(self.install)])
  contextfile=self.root/'context.json';contextfile.write_text(json.dumps(c));self.context,self.context_sha=stage.read_context(contextfile)
  self.now=datetime.now(timezone.utc);self.expires=self.now+timedelta(days=2);self.signers={r:CryptoSigner.generate_ed25519()for r in ('root','targets','snapshot','timestamp')};self.transport=Transport();self.base='https://updates.invalid/metadata/';self.target='https://updates.invalid/targets/'
  root=Metadata(Root(expires=self.expires,consistent_snapshot=False))
  for role,signer in self.signers.items():root.signed.add_key(signer.public_key,role)
  root.sign(self.signers['root']);self.bootstrap=root.to_bytes(JSONSerializer(compact=True))
  pe=bytearray(256);pe[:2]=b'MZ';pe[60:64]=(128).to_bytes(4,'little');pe[128:134]=b'PE\0\0\x64\x86';self.pe=bytes(pe)
  self.manifest=dict(schema='PGN_COMPONENT_STAGE_BUNDLE_V1',bundle_id='qa.cpu',sequence=2,release_version='1.0.0',network='testnet',genesis_hash='5'*64,platform='windows-x86_64',compatibility_set='qa.cohort',components=[dict(id='cpu',version='1.0.0',abi='qa.abi',primary='payload/cpu/controller.exe',dependencies=[],files=[dict(path='payload/cpu/controller.exe',bytes=len(self.pe),sha256=digest(self.pe))])])
  self.publish()
 def tearDown(self):
  self.assertEqual(self.sentinel.read_bytes(),b'PRIVATE SENTINEL NOT READ BY UPDATER');self.tmp.cleanup()
 def raw(self,metadata,role):metadata.sign(self.signers[role]);return metadata.to_bytes(JSONSerializer(compact=True))
 def publish(self,version=1,expired=False,bad_signature=False,archive_manifest=None):
  payload=io.BytesIO()
  with zipfile.ZipFile(payload,'w')as z:z.writestr('component-bundle.json',json.dumps(archive_manifest or self.manifest));z.writestr(self.manifest['components'][0]['primary'],self.pe)
  archive=payload.getvalue();catalog=json.dumps(dict(schema='PGN_AUTHENTICATED_UPDATE_CATALOG_V1',bundle_target='components.zip',manifest=self.manifest)).encode()
  targets=Metadata(Targets(version=version,expires=self.expires));targets.signed.targets={n:TargetFile.from_data(n,v,['sha256'])for n,v in [('components.json',catalog),('components.zip',archive)]}
  if bad_signature:targets.sign(CryptoSigner.generate_ed25519());traw=targets.to_bytes(JSONSerializer(compact=True))
  else:traw=self.raw(targets,'targets')
  snapshot=Metadata(Snapshot(version=version,expires=self.expires));snapshot.signed.meta['targets.json']=MetaFile.from_data(version,traw,['sha256']);sraw=self.raw(snapshot,'snapshot')
  timestamp=Metadata(Timestamp(version=version,expires=self.now-timedelta(seconds=10)if expired else self.expires));timestamp.signed.snapshot_meta=MetaFile.from_data(version,sraw,['sha256']);tsraw=self.raw(timestamp,'timestamp')
  self.transport.files={self.base+'timestamp.json':tsraw,self.base+'snapshot.json':sraw,self.base+'targets.json':traw,self.target+'components.json':catalog,self.target+'components.zip':archive}
 def client(self,context=None):return AuthenticatedUpdates(self.root/'cache',self.bootstrap,digest(self.bootstrap),self.base,self.target,context or self.context,self.context_sha,fetcher=self.transport)
 def test_signed_catalog_reports_versions_without_installing(self):
  value=self.client().refresh(['cpu']);self.assertTrue(value['publisher_authenticated']);self.assertEqual(value['components'][0]['version'],'1.0.0');self.assertFalse(value['installation_performed'])
 def test_authenticated_stage_is_idempotent_and_never_executes(self):
  client=self.client();a=client.prepare(['cpu']);b=client.prepare(['cpu']);self.assertEqual(a,b);self.assertEqual((Path(a['stage'])/'payload/cpu/controller.exe').read_bytes(),self.pe);self.assertTrue(a['publisher_authenticated']);self.assertFalse(a['process_control_performed'])
 def test_foreign_root_pin_rejected(self):
  with self.assertRaises(stage.StageError):AuthenticatedUpdates(self.root/'cache',self.bootstrap,'0'*64,self.base,self.target,self.context,self.context_sha,fetcher=self.transport)
 def test_short_cache_locator_still_checks_complete_root_identity(self):
  client=self.client();namespace=client.metadata.parent
  self.assertEqual(namespace.name,digest(self.bootstrap)[:20])
  self.assertEqual((namespace/'.bootstrap-sha256').read_text().strip(),digest(self.bootstrap))
  (namespace/'.bootstrap-sha256').write_bytes((digest(self.bootstrap)[:20]+'0'*44+'\n').encode())
  with self.assertRaisesRegex(stage.StageError,'full bootstrap root'):self.client()
 def test_prepared_paths_are_short_and_full_digests_remain_verified(self):
  result=self.client().prepare(['cpu']);folder=Path(result['stage'])
  self.assertEqual(len(folder.name),20);self.assertTrue(folder.parent.name.startswith('s-'));self.assertEqual(len(folder.parent.name),22)
  self.assertEqual(len(result['manifest_sha256']),64)
  raw=(folder/'component-bundle.json').read_bytes();self.assertEqual(digest(raw),result['manifest_sha256'])
 def test_wrong_network_refused_even_when_signed(self):
  self.manifest['genesis_hash']='6'*64;self.publish()
  with self.assertRaisesRegex(stage.StageError,'genesis_hash'):self.client().refresh(['cpu'])
 def test_foreign_targets_signer_refused(self):
  self.publish(bad_signature=True)
  with self.assertRaises(UnsignedMetadataError)as e:self.client().refresh(['cpu'])
  self.assertIn('targets',str(e.exception).lower())
 def test_expired_timestamp_refused(self):
  self.publish(expired=True)
  with self.assertRaises(ExpiredMetadataError)as e:self.client().refresh(['cpu'])
  self.assertIn('expired',str(e.exception).lower())
 def test_validly_signed_older_repository_refused(self):
  self.publish(version=2);self.client().refresh(['cpu']);self.publish(version=1)
  with self.assertRaises(BadVersionNumberError)as e:self.client().refresh(['cpu'])
  self.assertIn('version',str(e.exception).lower())
 def test_tampered_payload_refused(self):
  self.transport.files[self.target+'components.zip']+=b'CORRUPTED'
  with self.assertRaises(DownloadLengthMismatchError):self.client().prepare(['cpu'])
 def test_incomplete_cached_download_is_repaired_and_reverified(self):
  client=self.client();client.prepare(['cpu'])
  payload=next(client.targets.glob('*-components.zip'));payload.write_bytes(b'INCOMPLETE DOWNLOAD')
  result=client.prepare(['cpu'])
  self.assertTrue(result['publisher_authenticated']);self.assertEqual(client.cache_repairs,['components.zip'])
  self.assertGreater(payload.stat().st_size,len(b'INCOMPLETE DOWNLOAD'))
 def test_signed_catalog_cannot_describe_a_different_archive(self):
  other=deepcopy(self.manifest);other['components'][0]['version']='1.0.1';self.publish(archive_manifest=other)
  with self.assertRaisesRegex(stage.StageError,'different components'):self.client().prepare(['cpu'])
 def test_unknown_cache_link_refused(self):
  client=self.client();link=client.cache/'foreign-link'
  try:os.symlink(self.sentinel,link)
  except OSError as e:self.skipTest('OS does not allow creating the adversarial link: '+str(e.winerror))
  with self.assertRaisesRegex(stage.StageError,'Linked'):client.refresh(['cpu'])
 def test_tuf_root_alias_cannot_escape_its_history(self):
  client=self.client();client.refresh(['cpu']);alias=client.metadata/'root.json';alias.unlink()
  try:os.symlink(self.sentinel,alias)
  except OSError as e:self.skipTest('OS does not allow creating the adversarial link: '+str(e.winerror))
  with self.assertRaisesRegex(stage.StageError,'root alias'):client.refresh(['cpu'])
 def test_two_writers_cannot_share_update_cache(self):
  client=self.client()
  with locked_cache(client.cache):
   with self.assertRaises(OSError):client.refresh(['cpu'])
if __name__=='__main__':unittest.main()
