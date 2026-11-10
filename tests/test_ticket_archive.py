from pathlib import Path
import gzip,hashlib,json,sqlite3,sys,tempfile,unittest
from contextlib import closing
from unittest.mock import patch
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'src/engine/runtime_host'),str(P/'src/protocol')]
import horizon_ticket_archive as archive
class TicketArchiveTests(unittest.TestCase):
 def test_changed_gold_classification_reuses_exact_computation_without_rewriting_archive(self):
  self.cfg.update(target_merit_q16=1810202,target_merit=1810202/65536)
  (self.folder/'session.json').write_bytes(archive.canonical(self.meta));self.compact()
  before={p.name:p.read_bytes()for p in self.folder.iterdir()if p.is_file()}
  current=dict(self.cfg,target_merit_q16=1791729,target_merit=1791729/65536)
  self.assertEqual(archive.result(self.folder,current),self.report)
  self.assertEqual({p.name:p.read_bytes()for p in self.folder.iterdir()if p.is_file()},before)
  for key,value in [('start','150000000000000000001'),('min_gap',956),('preseal_required',True),('report_top',99)]:
   with self.subTest(key=key),self.assertRaises(ValueError):archive.result(self.folder,dict(current,**{key:value}))
  with self.assertRaises(ValueError):archive.result(self.folder,dict(current,target_merit=21))
 def test_scientific_finalization_keeps_original_configuration_binding_after_gold_retarget(self):
  from horizon_engine_host import EngineService
  self.cfg.update(target_merit_q16=1810202,target_merit=1810202/65536)
  (self.folder/'session.json').write_bytes(archive.canonical(self.meta));self.compact()
  value=dict(schema='PGN_SCOPED_SOURCE_FINALIZATION_V1',ticket_id=self.folder.name,
   configuration_sha256=hashlib.sha256(archive.canonical(self.cfg)).hexdigest(),
   original_report_sha256=self.report['sha256'],corrected_report=self.report)
  raw=archive.canonical(value);(self.folder/'source-finalization.json').write_bytes(raw);(self.folder/'source-finalization.sha256').write_bytes(hashlib.sha256(raw).hexdigest().encode()+b'\n')
  service=EngineService.__new__(EngineService);current=dict(self.cfg,target_merit_q16=1791729,target_merit=1791729/65536)
  self.assertEqual(service._cached_finalization(self.folder,current,self.report),self.report)
  self.assertEqual((self.folder/'source-finalization.json').read_bytes(),raw)
  with self.assertRaises(ValueError):service._cached_finalization(self.folder,dict(current,min_gap=976),self.report)
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.folder=Path(self.tmp.name)/'tickets'/('1'*64);self.folder.mkdir(parents=True)
  self.cfg=dict(ticket_id=self.folder.name,start='150000000000000000000',end='150000200000000000000',threshold_policy=dict(sha256='a'*64))
  computed=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=self.folder.name,start=self.cfg['start'],end=self.cfg['end'],positions='200000000000000',policy_sha256='a'*64,counts=['3']+['0']*99,best=[])
  self.report=dict(body=computed,sha256=hashlib.sha256(archive.canonical(computed)).hexdigest(),network_accepted=False)
  self.meta=dict(schema='PGN68_DURABLE_SESSION_V1',state='COMPLETE',current_chunk=None,configuration=self.cfg)
  self.frame=dict(ticket_id=self.folder.name,start=self.cfg['start'],end=self.cfg['end'],positions=computed['positions'],phase='Report ready',observed_utc='2026-11-01T10:00:00+00:00',speed_rate=123,scopes=dict(ticket=dict(statistics=dict(count_total='3'))))
  self.original=archive.canonical(self.meta);(self.folder/'session.json').write_bytes(self.original);(self.folder/'report.json').write_bytes(archive.canonical(self.report))
  for name in archive.WORK_FILES:(self.folder/name).write_bytes(b'redundant fully committed data')
 def tearDown(self):self.tmp.cleanup()
 def valid_index(self):
  # Every completed engine now conserves its exact index before retirement.
  path=self.folder/'session-index.sqlite3'
  if path.exists()and path.read_bytes()[:16]!=b'SQLite format 3\0':
   path.unlink()
   for suffix in ('-wal','-shm'):(self.folder/('session-index.sqlite3'+suffix)).write_bytes(b'')
   with closing(sqlite3.connect(path))as db:
    with db:
     db.executescript('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT);CREATE TABLE gaps(lo BLOB PRIMARY KEY,hi BLOB,gap INTEGER,seq INTEGER,bin INTEGER);')
     db.execute('INSERT INTO meta VALUES(?,?)',('binding',json.dumps(dict(configuration_hash=hashlib.sha256(archive.canonical(self.cfg)).hexdigest()))))
     start=int(self.cfg['start'])
     db.executemany('INSERT INTO gaps VALUES(?,?,?,?,?)',[((start+i*20).to_bytes(16,'big'),(start+i*20+10).to_bytes(16,'big'),10,i,0)for i in range(3)])
 def compact(self):
  self.valid_index()
  with patch('horizon_ticket_archive.read_session_metadata',return_value=self.meta),patch('horizon_status_stream.ticket_frame',return_value=self.frame):return archive.compact(self.folder)
 def intact(self):
  self.assertEqual((self.folder/'session.json').read_bytes(),self.original)
  self.assertTrue(all((self.folder/n).exists()for n in archive.WORK_FILES))
 def test_compact_and_exact_result(self):
  out=self.compact();self.assertTrue(out['compacted']);self.assertGreater(out['reclaimed_bytes'],0);self.assertEqual(archive.result(self.folder,self.cfg),self.report)
  self.assertFalse(any((self.folder/n).exists()for n in archive.WORK_FILES));self.assertTrue((self.folder/'report.json').exists())
  frame=archive.frame(self.folder);self.assertTrue(frame['archived']);self.assertEqual(frame['source_observed_utc'],self.frame['observed_utc']);self.assertIsNone(frame['speed_rate']);self.assertEqual(frame['scopes'],self.frame['scopes'])
 def test_active_and_recoverable_retained(self):
  for state,chunk in [('RUNNING',None),('COMPLETE',{'unfinished':True})]:
   self.meta.update(state=state,current_chunk=chunk);self.assertFalse(self.compact()['compacted']);self.intact()
 def test_dry_run_retains_original(self):
  with patch('horizon_ticket_archive.read_session_metadata',return_value=self.meta),patch('horizon_status_stream.ticket_frame',return_value=self.frame):out=archive.compact(self.folder,dry_run=True)
  self.assertTrue(out['dry_run']);self.assertGreater(out['reclaimable_bytes'],0);self.intact();self.assertFalse((self.folder/archive.ARCHIVE_NAME).exists())
 def test_live_owner_retained(self):
  owner=self.folder/('native-owner-'+'e'*32);owner.mkdir();self.assertFalse(self.compact()['compacted']);self.intact()
 def test_unsettled_owner_retained(self):
  owner=self.folder/('native-owner-'+'e'*32);owner.mkdir();(owner/'done.json').write_text(json.dumps(dict(closed=True,exit=1,error='failed')));self.assertFalse(self.compact()['compacted']);self.intact()
 def test_interruption_before_commit(self):
  real=archive.atomic_bytes
  def fail_marker(path,data):
   if Path(path).name=='session.json':raise OSError('injected marker failure')
   real(path,data)
  with patch('horizon_ticket_archive.atomic_bytes',side_effect=fail_marker),self.assertRaises(OSError):self.compact()
  self.intact();self.assertIsNone(archive.load(self.folder));self.assertTrue(self.compact()['compacted'])
 def test_interruption_after_commit(self):
  with patch('horizon_ticket_archive._purge_work_files',side_effect=OSError('injected cleanup failure')),self.assertRaises(OSError):self.compact()
  self.assertEqual(archive.result(self.folder,self.cfg),self.report);self.assertTrue(all((self.folder/n).exists()for n in archive.WORK_FILES));self.assertTrue(archive.compact(self.folder)['already_compacted'])
 def test_idempotent(self):
  self.compact();before=(self.folder/archive.ARCHIVE_NAME).read_bytes();self.assertTrue(archive.compact(self.folder)['already_compacted']);self.assertEqual((self.folder/archive.ARCHIVE_NAME).read_bytes(),before)
 def test_configuration_mismatch_refused(self):
  self.compact();changed=dict(self.cfg,start='150000000000000000001')
  with self.assertRaises(ValueError):archive.result(self.folder,changed)
 def test_archive_corruption_refused(self):
  self.compact();path=self.folder/archive.ARCHIVE_NAME;path.write_bytes(path.read_bytes()+b'wrong')
  with self.assertRaises(ValueError):archive.load(self.folder)
 def test_decoded_budget(self):
  self.compact();path=self.folder/archive.ARCHIVE_NAME;encoded=gzip.compress(b'x'*(archive.MAXIMUM_BYTES+1),mtime=0);path.write_bytes(encoded);marker=json.loads((self.folder/'session.json').read_bytes());marker['sha256']=hashlib.sha256(encoded).hexdigest();(self.folder/'session.json').write_text(json.dumps(marker))
  with self.assertRaises(ValueError):archive.load(self.folder)
 def test_count_mismatch_retained(self):
  self.frame['scopes']['ticket']['statistics']['count_total']='4'
  with self.assertRaises(ValueError):self.compact()
  self.intact()
 def test_sealed_report_mismatch_retained(self):
  network=dict(body=dict(computed={'changed':True}),report_root='a'*64);(self.folder/'network-report.json').write_text(json.dumps(network))
  with self.assertRaises(ValueError):self.compact()
  self.intact()
 def test_folder_escape_refused(self):
  with self.assertRaises(ValueError):archive.compact(Path(self.tmp.name))
if __name__=='__main__':unittest.main()
