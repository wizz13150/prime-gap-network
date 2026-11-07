"""Exact position conservation, not a primality or coverage certificate."""
from pathlib import Path
from contextlib import closing,contextmanager
import gzip,hashlib,json,sqlite3,sys,tempfile,unittest
from unittest.mock import patch
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'src/engine/runtime_host'))
import horizon_scientific_gaps as science

class ScientificGapConservationTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.folder=Path(self.temp.name)/'tickets'/('1'*64);self.folder.mkdir(parents=True)
  self.cfg=dict(ticket_id=self.folder.name,start=str(2**94),end=str(2**94+10**14),threshold_policy=dict(sha256='a'*64,minimum_merit_numerator=205,minimum_merit_denominator=10))
  self.index=self.folder/'session-index.sqlite3'
  with self.database()as connection:
   connection.executescript('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT);CREATE TABLE gaps(lo BLOB PRIMARY KEY,hi BLOB,gap INTEGER,seq INTEGER,bin INTEGER,visible INTEGER,count_ok INTEGER,detail_ok INTEGER);')
   connection.execute('INSERT INTO meta VALUES(?,?)',('binding',json.dumps(dict(configuration_hash=hashlib.sha256(science.canonical(self.cfg)).hexdigest(),session_id='fixture'))))
  self.rows=[]
 def tearDown(self):self.temp.cleanup()
 @contextmanager
 def database(self):
  with closing(sqlite3.connect(self.index))as connection:
   with connection:yield connection
 def populate(self,count):
  self.rows=[[str(2**94+i*2048),str(2**94+i*2048+1200),1200,i,i%100,1,1,1]for i in range(count)]
  with self.database()as connection:
   connection.executemany('INSERT INTO gaps VALUES(?,?,?,?,?,?,?,?)',[(int(r[0]).to_bytes(16,'big'),int(r[1]).to_bytes(16,'big'),*r[2:])for r in self.rows])
 def test_all_ten_thousand_rows_beyond_best100_and_95bit_positions_survive(self):
  self.populate(10001);before=self.index.read_bytes();descriptor=science.create(self.folder,self.cfg)
  self.assertEqual(descriptor['records'],10001);self.assertEqual(science.verify(self.folder,descriptor,self.cfg),descriptor)
  with gzip.open(self.folder/science.NAME,'rt',encoding='utf-8')as stream:lines=[json.loads(line)for line in stream]
  self.assertEqual(lines[1:-1],self.rows);self.assertEqual(lines[0]['collection_policy'],self.cfg['threshold_policy']);self.assertFalse(lines[0]['network_acceptance_proven'])
  self.assertEqual(self.index.read_bytes(),before);self.assertLess(descriptor['bytes'],len(science.canonical(self.rows)))
 def test_empty_observation_set_is_explicit(self):
  descriptor=science.create(self.folder,self.cfg);self.assertEqual(descriptor['records'],0);science.verify(self.folder,descriptor,self.cfg)
 def test_crash_before_descriptor_publication_reuses_identical_sidecar(self):
  self.populate(8);first=science.create(self.folder,self.cfg);raw=(self.folder/science.NAME).read_bytes()
  self.assertEqual(science.create(self.folder,self.cfg),first);self.assertEqual((self.folder/science.NAME).read_bytes(),raw)
 def test_corruption_is_not_overwritten_and_original_index_is_retained(self):
  self.populate(3);descriptor=science.create(self.folder,self.cfg);(self.folder/science.NAME).write_bytes(b'CORRUPTED');before=self.index.read_bytes()
  with self.assertRaisesRegex(ValueError,'archive .* differs'):science.verify(self.folder,descriptor,self.cfg)
  with self.assertRaisesRegex(ValueError,'Existing scientific'):science.create(self.folder,self.cfg)
  self.assertEqual(self.index.read_bytes(),before);self.assertEqual((self.folder/science.NAME).read_bytes(),b'CORRUPTED')
 def test_foreign_binding_keeps_originals(self):
  with self.database()as connection:connection.execute("UPDATE meta SET v=? WHERE k='binding'",(json.dumps(dict(configuration_hash='b'*64)),))
  before=self.index.read_bytes()
  with self.assertRaisesRegex(ValueError,'binding'):science.create(self.folder,self.cfg)
  self.assertEqual(self.index.read_bytes(),before);self.assertFalse((self.folder/science.NAME).exists())
 def test_invalid_gap_arithmetic_never_commits_an_archive(self):
  self.populate(1)
  with self.database()as connection:connection.execute('UPDATE gaps SET gap=1198')
  with self.assertRaisesRegex(ValueError,'arithmetic'):science.create(self.folder,self.cfg)
  self.assertFalse((self.folder/science.NAME).exists());self.assertTrue(self.index.is_file());self.assertFalse(list(self.folder.glob('*.pending')))
 def test_storage_bound_keeps_originals_and_removes_only_owned_partial_file(self):
  self.populate(3);before=self.index.read_bytes()
  with patch.object(science,'MAX_FILE',32):
   with self.assertRaisesRegex(ValueError,'budget'):science.create(self.folder,self.cfg)
  self.assertEqual(self.index.read_bytes(),before);self.assertFalse((self.folder/science.NAME).exists());self.assertFalse(list(self.folder.glob('*.pending')))
if __name__=='__main__':unittest.main()
