"""Exact read-only completed generation lookup, real SQLite/WAL, no worker."""
from pathlib import Path
import json,sqlite3,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from pgn_index import read_closed_chunk
class ClosedChunkLookupTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.path=Path(self.tmp.name)/'session-index.sqlite3';self.sid='a'*32;self.digest='b'*64
  self.db=sqlite3.connect(self.path);self.addCleanup(self.db.close);self.db.execute('PRAGMA journal_mode=WAL');self.db.execute('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT NOT NULL)');self.db.execute('CREATE TABLE chunks(idx INTEGER PRIMARY KEY,value TEXT NOT NULL)');self.db.execute('INSERT INTO meta VALUES(?,?)',('binding',json.dumps(dict(session_id=self.sid,configuration_hash=self.digest))));self.db.commit()
 def read(self,index):return read_closed_chunk(self.path,index,self.sid,self.digest)
 def test_primary_key_lookup_never_decodes_unrelated_history(self):
  value=dict(index=1000,instance=dict(work_closed=True,job_nonce='c'*32));self.db.execute('INSERT INTO chunks VALUES(?,?)',(1,'invalid unrelated JSON'));self.db.execute('INSERT INTO chunks VALUES(?,?)',(1000,json.dumps(value)));self.db.commit();self.assertEqual(self.read(1000),value);self.assertIsNone(self.read(999));plan=self.db.execute('EXPLAIN QUERY PLAN SELECT value FROM chunks WHERE idx=?',(1000,)).fetchone()[-1];self.assertIn('PRIMARY KEY',plan)
 def test_only_committed_WAL_receipt_is_visible(self):
  value=dict(index=2,instance=dict(work_closed=True));self.db.execute('INSERT INTO chunks VALUES(?,?)',(2,json.dumps(value)));self.assertIsNone(self.read(2));self.db.commit();self.assertGreater(self.path.with_name(self.path.name+'-wal').stat().st_size,0);self.assertEqual(self.read(2),value)
 def test_replaced_session_or_configuration_is_refused(self):
  for sid,digest in [('c'*32,self.digest),(self.sid,'d'*64)]:
   with self.assertRaisesRegex(ValueError,'different session'):read_closed_chunk(self.path,0,sid,digest)
 def test_no_file_creation_and_strict_index(self):
  missing=self.path.with_name('not-present.sqlite3');self.assertIsNone(read_closed_chunk(missing,0,self.sid,self.digest));self.assertFalse(missing.exists())
  for index in [True,-1,2**63,'1']:
   with self.assertRaisesRegex(ValueError,'Invalid completed chunk'):self.read(index)
 def test_corrupt_identity_or_oversized_receipt_is_refused(self):
  self.db.execute('INSERT INTO chunks VALUES(?,?)',(3,json.dumps(dict(index=4))));self.db.execute('INSERT INTO chunks VALUES(?,?)',(5,json.dumps(dict(index=5,payload='x'*65536))));self.db.commit()
  for index in [3,5]:
   with self.assertRaises(ValueError):self.read(index)
if __name__=='__main__':unittest.main()
