"""A still-canonical historical anchor survives ordinary forward progress."""
from pathlib import Path
import json,sys,tempfile,unittest
P=Path(__file__).resolve().parents[1];sys.path.insert(0,str(P/'src/engine/runtime_host'))
from horizon_owner_reports import OwnerReports
class AdvancingRPC:
 def __init__(self,*,reorg=False,genesis='a'*64):
  self.tip='2'*64;self.genesis=genesis;self.reorg=reorg;self.map={0:genesis,1:'1'*64,2:self.tip}
 def call(self,method,params=None):
  if method=='getbestblockhash':return self.tip
  if method=='getblockheader':
   if params==['2'*64]:
    self.tip='3'*64
    if self.reorg:self.map[2]='4'*64
    return dict(height=2)
   raise AssertionError(params)
  if method=='getblockhash':return self.map[params[0]]
  raise AssertionError(method)
class QueueAnchorAdvanceTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.q=OwnerReports(Path(self.tmp.name))
  self.q.register('b'*64,dict(ticket='c'*64,report_root='d'*64))
 def meta(self):
  with self.q.database()as db:return dict(db.execute('SELECT key,value FROM meta'))
 def test_forward_tip_during_read_does_not_starve_queue(self):
  rpc=AdvancingRPC();anchor=self.q.observe(rpc)
  self.assertEqual(rpc.tip,'3'*64)
  self.assertEqual(anchor,dict(hash='2'*64,height=2,genesis='a'*64))
  rows,_=self.q.next_on_anchor(anchor);self.assertEqual([x[0]for x in rows],['b'*64])
 def test_reorganized_snapshot_is_refused_without_queue_anchor_write(self):
  before=self.meta();self.assertIsNone(self.q.observe(AdvancingRPC(reorg=True)));self.assertEqual(self.meta(),before)
 def test_foreign_chain_refused_without_rebinding(self):
  with self.q.database()as db:db.execute("INSERT INTO meta VALUES('genesis',?)",('f'*64,))
  before=self.meta()
  with self.assertRaisesRegex(ValueError,'another network'):self.q.observe(AdvancingRPC())
  self.assertEqual(self.meta(),before)
 def test_normal_growth_keeps_terminal_acknowledgement(self):
  with self.q.database()as db:
   db.execute('UPDATE reports SET waiting=0');db.execute("INSERT INTO meta VALUES('anchor',?)",(json.dumps(dict(hash='1'*64,height=1,genesis='a'*64)),))
  self.assertIsNotNone(self.q.observe(AdvancingRPC()))
  with self.q.database()as db:self.assertEqual(db.execute('SELECT waiting FROM reports').fetchone()[0],0)
 def test_prior_orphan_reopens_but_does_not_remove_result(self):
  with self.q.database()as db:
   db.execute('UPDATE reports SET waiting=0');db.execute("INSERT INTO meta VALUES('anchor',?)",(json.dumps(dict(hash='e'*64,height=1,genesis='a'*64)),))
  self.assertIsNotNone(self.q.observe(AdvancingRPC()))
  with self.q.database()as db:self.assertEqual(db.execute('SELECT name,ticket,root,waiting FROM reports').fetchone(),('b'*64,'c'*64,'d'*64,1))
if __name__=='__main__':unittest.main()
