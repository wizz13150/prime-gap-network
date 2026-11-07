"""Exact archival exports: conservation, exposure and private-data boundaries."""
from pathlib import Path
import csv,gzip,hashlib,io,json,sqlite3,subprocess,sys,tempfile,unittest,zipfile
from contextlib import closing
from unittest.mock import patch
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'tools'),str(P/'src/engine/runtime_host'),str(P/'src/protocol')]
import export_gap_observations as export
import horizon_scientific_gaps as science
import horizon_ticket_archive as archive
from report_commitment import seal_report

GENESIS='7'*64
SECRET='NEVER_EXPORT_THIS_RPC_SECRET_OR_WALLET_KEY'


class GapObservationExportTests(unittest.TestCase):
 def setUp(self):
  temp=tempfile.TemporaryDirectory();self.addCleanup(temp.cleanup);self.root=Path(temp.name)
  self.output=self.root/'observations.zip'
 def ticket(self,char='1',start=2**94+1,count=111):
  folder=self.root/'tickets'/(char*64);folder.mkdir(parents=True)
  cfg=dict(ticket_id=folder.name,start=str(start),end=str(start+10**14),threshold_policy=dict(sha256='a'*64),rpc_password=SECRET)
  values=[(start+i*4096,start+i*4096+2000)for i in range(count)]
  index=folder/'session-index.sqlite3'
  with closing(sqlite3.connect(index))as db:
   with db:
    db.executescript('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT);CREATE TABLE gaps(lo BLOB PRIMARY KEY,hi BLOB,gap INTEGER,seq INTEGER,bin INTEGER);')
    db.execute('INSERT INTO meta VALUES(?,?)',('binding',json.dumps(dict(configuration_hash=hashlib.sha256(science.canonical(cfg)).hexdigest()))))
    db.executemany('INSERT INTO gaps VALUES(?,?,?,?,?)',[(lo.to_bytes(16,'big'),hi.to_bytes(16,'big'),hi-lo,i,0)for i,(lo,hi)in enumerate(values)])
  descriptor=science.create(folder,cfg)
  body=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=folder.name,start=cfg['start'],end=cfg['end'],positions=str(10**14),policy_sha256='a'*64,counts=[str(count)]+['0']*99,best=[dict(gap=hi-lo,lower_prime=str(lo),upper_prime=str(hi))for lo,hi in values[:100]])
  report=dict(body=body,sha256=hashlib.sha256(archive.canonical(body)).hexdigest(),network_accepted=False)
  work=dict(start=cfg['start'],length=body['positions'],ticket=folder.name,genesis=GENESIS,request_id='b'*64,work_id='c'*64,target_numerator=32,target_denominator=1)
  (folder/'network-report.json').write_bytes(archive.canonical(seal_report(report,work,nonce='d'*64,discoverer='PGN')))
  frame=dict(ticket_id=folder.name,start=cfg['start'],end=cfg['end'],positions=body['positions'],phase='Report ready',work_role='search',scopes=dict(ticket=dict(statistics=dict(count_total=str(count)))))
  compact=dict(schema=archive.SCHEMA,configuration=cfg,configuration_sha256=hashlib.sha256(archive.canonical(cfg)).hexdigest(),report=report,frame=frame,scientific_gap_archive=descriptor)
  self.write_compact(folder,compact)
  (folder/'wallet.dat').write_text(SECRET);(folder/'.cookie').write_text(SECRET)
  return folder,compact,values
 def write_compact(self,folder,body):
  encoded=gzip.compress(archive.canonical(body),mtime=0);(folder/archive.ARCHIVE_NAME).write_bytes(encoded)
  (folder/'session.json').write_bytes(archive.canonical(dict(schema=archive.SCHEMA,ticket_id=folder.name,archive=archive.ARCHIVE_NAME,sha256=hashlib.sha256(encoded).hexdigest())))
 def hashes(self,folder):return {p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in folder.iterdir()if p.is_file()}
 def test_ten_thousand_exact_95bit_pairs_histograms_and_source_unchanged(self):
  folder,body,values=self.ticket(count=10001);before=self.hashes(folder)
  result=export.create(self.output,[folder],GENESIS);self.assertEqual(result['records'],10001);self.assertFalse(result['network_acceptance_proven']);self.assertEqual(before,self.hashes(folder))
  with zipfile.ZipFile(self.output)as package:
   manifest=json.loads(package.read('manifest.json'));rows=list(csv.DictReader(io.StringIO(package.read('observations.csv').decode())))
   self.assertEqual([(r['lower_prime'],r['upper_prime'])for r in rows],[(str(lo),str(hi))for lo,hi in values])
   self.assertEqual(rows[0]['count_ok'],'');self.assertEqual(manifest['tickets'][0]['collection_policy'],body['configuration']['threshold_policy'])
   self.assertTrue(manifest['tickets'][0]['genesis_bound_to_sealed_report']);self.assertEqual(manifest['population'],export.POPULATION)
   sizes=list(csv.DictReader(io.StringIO(package.read('gap-sizes.csv').decode())));self.assertEqual(sizes,[dict(gap='2000',count='10001')])
   bands=list(csv.DictReader(io.StringIO(package.read('merit-bands.csv').decode())));self.assertEqual(sum(int(r['count'])for r in bands),10001)
   for name in package.namelist():self.assertNotIn(SECRET.encode(),package.read(name))
  self.assertTrue(export.verify(self.output,GENESIS)['inventory_verified'])
 def test_empty_population_is_explicit(self):
  folder,_,_=self.ticket(count=0);result=export.create(self.output,[folder],GENESIS);self.assertEqual(result['records'],0)
  with zipfile.ZipFile(self.output)as package:self.assertEqual(len(package.read('gap-sizes.csv').splitlines()),1)
 def test_disjoint_ranges_aggregate_without_duplicate_exposure(self):
  first,_,_=self.ticket(count=3);second,_,_=self.ticket('2',2**94+10**14+1,count=4)
  self.assertEqual(export.create(self.output,[first,second],GENESIS)['records'],7)
 def test_duplicate_and_overlapping_ranges_are_not_double_counted(self):
  first,_,_=self.ticket();second,_,_=self.ticket('2')
  for folders in ([first,first],[first,second]):
   with self.assertRaisesRegex(ValueError,'Duplicate|Overlapping'):export.create(self.output,folders,GENESIS)
   self.assertFalse(self.output.exists())
 def test_legacy_summary_does_not_invent_missing_observations(self):
  folder,body,_=self.ticket();del body['scientific_gap_archive'];self.write_compact(folder,body)
  with self.assertRaisesRegex(ValueError,'unavailable'):export.create(self.output,[folder],GENESIS)
  self.assertFalse(self.output.exists())
 def test_corrupt_sidecar_and_foreign_genesis_refuse_publication(self):
  folder,_,_=self.ticket()
  with self.assertRaises(ValueError):export.create(self.output,[folder],'8'*64)
  (folder/science.NAME).write_bytes(b'broken')
  with self.assertRaises(ValueError):export.create(self.output,[folder],GENESIS)
  self.assertFalse(self.output.exists())
 def test_inflight_source_change_leaves_no_partial_export(self):
  folder,_,_=self.ticket(count=3);actual=export.rows
  def changed(*args):
   for i,row in enumerate(actual(*args)):
    yield row
    if i==0:(folder/'session.json').write_bytes(b'changed while reading')
  with patch.object(export,'rows',side_effect=changed),self.assertRaisesRegex(ValueError,'changed'):export.create(self.output,[folder],GENESIS)
  self.assertFalse(self.output.exists());self.assertFalse(list(self.root.glob('.pgn-gap-export-*')))
 def test_bounds_preserve_sources_and_previous_export(self):
  folder,_,_=self.ticket(count=3);before=self.hashes(folder)
  with patch.object(export,'MAX_RECORDS',2),self.assertRaisesRegex(ValueError,'record budget'):export.create(self.output,[folder],GENESIS)
  self.assertEqual(before,self.hashes(folder));self.assertFalse(list(self.root.glob('.pgn-gap-export-*')))
  export.create(self.output,[folder],GENESIS);saved=self.output.read_bytes()
  with self.assertRaisesRegex(ValueError,'new ZIP'):export.create(self.output,[folder],GENESIS)
  self.assertEqual(self.output.read_bytes(),saved)
 def test_offline_checksum_detects_changed_csv(self):
  folder,_,_=self.ticket(count=3);export.create(self.output,[folder],GENESIS);bad=self.root/'changed.zip'
  with zipfile.ZipFile(self.output)as source,zipfile.ZipFile(bad,'w')as target:
   for name in source.namelist():
    raw=source.read(name)
    if name=='observations.csv':raw=raw.replace(b'2000',b'2002')
    target.writestr(name,raw)
  with self.assertRaisesRegex(ValueError,'checksum'):export.verify(bad,GENESIS)
 def test_real_compaction_correction_is_exported_without_disguising_raw_counts(self):
  sys.path.insert(0,str(P/'tests'))
  from test_archive_reconciliation import ArchiveReconciliationTests
  fixture=ArchiveReconciliationTests('runTest');fixture.setUp();self.addCleanup(fixture.doCleanups)
  self.assertTrue(fixture.compact()['compacted']);before=self.hashes(fixture.folder)
  result=export.create(self.output,[fixture.folder],fixture.work['genesis'])
  self.assertEqual(result['records'],111);self.assertEqual(before,self.hashes(fixture.folder))
  with zipfile.ZipFile(self.output)as package:
   prefix='tickets/'+fixture.folder.name+'/'
   marker=json.loads(package.read(prefix+'source-finalization.json'))
   values=list(csv.DictReader(io.StringIO(package.read('observations.csv').decode())))
   observed={(int(row['lower_prime']),int(row['upper_prime']))for row in values}
   removed={tuple(map(int,pair))for pair in marker['removed']};added={tuple(map(int,pair))for pair in marker['added']}
   self.assertEqual((observed-removed)|added,set(fixture.true));self.assertEqual(len((observed-removed)|added),110)
   manifest=json.loads(package.read('manifest.json'));self.assertTrue(manifest['tickets'][0]['finalized_correction_included'])
   self.assertEqual(manifest['population'],export.POPULATION)
 def test_cli_exports_and_checks_with_standard_library_runtime(self):
  folder,_,_=self.ticket(count=3)
  for extra in (['--output',str(self.output),'--ticket',str(folder)],['--verify',str(self.output)]):
   result=subprocess.run([sys.executable,str(P/'tools/export_gap_observations.py'),'--genesis',GENESIS,*extra],capture_output=True,timeout=15)
   self.assertEqual(result.returncode,0,result.stderr.decode());self.assertEqual(json.loads(result.stdout)['records'],3)
 def test_storage_and_histogram_bounds_never_publish_partial_files(self):
  folder,_,_=self.ticket(count=3)
  for name,value in [('MAX_ZIP',16),('MAX_TOTAL',16),('MAX_KEYS',0)]:
   with patch.object(export,name,value),self.assertRaisesRegex(ValueError,'budget'):export.create(self.output,[folder],GENESIS)
   self.assertFalse(self.output.exists());self.assertFalse(list(self.root.glob('.pgn-gap-export-*')))


if __name__=='__main__':unittest.main()
