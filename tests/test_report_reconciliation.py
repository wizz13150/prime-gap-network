from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_report_outbox import ReportOutbox
from horizon_report_reconciliation import park_orphaned_report,retry_one_parked_report
from horizon_mining_flow import MiningFlow
from horizon_core_rpc import CoreRpcError
from report_commitment import digest

class ReportReconciliationTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.out=ReportOutbox(self.root)
  self.work=json.loads((ROOT/'tests/fixtures/network/issued-work.json').read_bytes());self.work['parent']='b'*64
  result=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_bytes())
  (self.root/'tickets'/self.work['ticket']).mkdir(parents=True)
  self.entry=self.out.put(self.work,result);self.original=(self.root/'tickets'/self.work['ticket']/'network-report.json').read_bytes()
  self.tip='a'*64;self.confirmations=-1;self.canonical='c'*64;self.present=False;self.report_present=False;self.moving=False;self.wrong_range=False
  self.worker=SimpleNamespace(rpc=SimpleNamespace(call=self.call))
 def call(self,method,args=None):
  if method=='getblockchaininfo':return dict(bestblockhash=self.tip,blocks=10)
  if method=='getbestblockhash':return 'd'*64 if self.moving else self.tip
  if method=='getblockheader':return dict(hash=args[0],height=8,confirmations=self.confirmations)
  if method=='getblockhash':return self.canonical
  if method=='getpgnworkview':
   work=self.work;leases=[dict(request=work['request_id'],start=work['start'],end=str(int(work['start'])+int(work['length'])))]if self.present else[]
   return dict(schema='PGN_WORK_VIEW_V1',kind='tickets',request_id=args[-1],state=dict(genesis=work['genesis'],bestblockhash=self.tip,height=10,leases=leases),reports=[{}]if self.report_present else[])
  if method=='getpgnscientificwork':return dict(self.work,start='2'if self.wrong_range else self.work['start'])
  raise AssertionError(method)
 def record(self):return self.root/'report-reconciliation'/(self.work['request_id']+'.json')
 def test_proven_orphan_releases_slot_but_keeps_exact_scientific_payload(self):
  self.assertTrue(park_orphaned_report(self.worker,self.out,self.entry));self.assertEqual([],self.out._read())
  self.assertEqual((self.root/'tickets'/self.work['ticket']/'network-report.json').read_bytes(),self.original)
  value=json.loads(self.record().read_bytes());self.assertEqual(value['entry'],self.entry);self.assertEqual(value['status'],'orphaned_issuance')
  self.assertNotIn('accepted',value);self.assertEqual(json.loads((self.root/'report-reconciliation-index.json').read_bytes())['items'],[self.work['request_id']])
 def test_active_unknown_moving_or_still_reserved_work_is_never_parked(self):
  for attr,value in [('confirmations',1),('canonical','b'*64),('moving',True),('present',True),('report_present',True)]:
   previous=getattr(self,attr);setattr(self,attr,value)
   self.assertFalse(park_orphaned_report(self.worker,self.out,self.entry),attr)
   self.assertEqual(self.out.first(),self.entry);self.assertFalse(self.record().exists());setattr(self,attr,previous)
 def test_transport_failure_never_means_orphan(self):
  def fail(*_):raise OSError('Injected Core read failure')
  self.worker.rpc.call=fail
  with self.assertRaises(OSError):park_orphaned_report(self.worker,self.out,self.entry)
  self.assertEqual(self.out.first(),self.entry);self.assertFalse(self.record().exists())
 def test_repeated_handoff_after_crash_is_idempotent_and_retains_original(self):
  self.assertTrue(park_orphaned_report(self.worker,self.out,self.entry))
  self.out._save([self.entry]) # Simulate interruption after durable receipt, before queue release.
  self.assertTrue(park_orphaned_report(self.worker,self.out,self.entry));self.assertEqual([],self.out._read())
  self.assertEqual(json.loads((self.root/'report-reconciliation-index.json').read_bytes())['items'],[self.work['request_id']])
 def test_chain_return_restores_exact_original_for_normal_publication_not_acceptance(self):
  park_orphaned_report(self.worker,self.out,self.entry)
  self.assertFalse(retry_one_parked_report(self.worker,self.out));self.assertEqual([],self.out._read())
  self.present=True;self.wrong_range=True;self.assertFalse(retry_one_parked_report(self.worker,self.out))
  self.wrong_range=False;self.assertTrue(retry_one_parked_report(self.worker,self.out));self.assertEqual(self.out.first(),self.entry)
  self.assertEqual(json.loads(self.record().read_bytes())['status'],'restored_for_publication')
  self.assertFalse(retry_one_parked_report(self.worker,self.out));self.assertEqual(len(self.out._read()),1)
 def test_payload_tampering_cannot_free_a_queue_slot(self):
  path=self.root/'tickets'/self.work['ticket']/'network-report.json';value=json.loads(path.read_bytes());value['body']['request_id']='f'*64;path.write_text(json.dumps(value))
  with self.assertRaises(ValueError):park_orphaned_report(self.worker,self.out,self.entry)
  self.assertEqual(self.out.first(),self.entry);self.assertFalse(self.record().exists())
 def test_real_publisher_parks_orphan_then_publishes_all_three_valid_successors(self):
  for i in range(3):
   work=dict(self.work,request_id=f'{100+i:064x}',ticket=f'{200+i:064x}')
   result=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_bytes());result['body']['ticket_id']=work['ticket'];result['sha256']=digest(result['body'])
   (self.root/'tickets'/work['ticket']).mkdir();self.out.put(work,result)
  flow=MiningFlow.__new__(MiningFlow);flow.outbox=self.out;flow.recovered_report=None;flow.finalizer=None;flow.changed=threading.Event();flow.audit_lanes=[];flow.cycles=SimpleNamespace(reorg_held=lambda:[]);flow.report_ready=lambda _:True
  flow.worker=self.worker;self.worker.root=self.root;self.worker.quitting=threading.Event();self.worker.network_lock=threading.RLock();self.worker.audits=SimpleNamespace(reveal_owner_reports=lambda:None,reveal_committed=lambda:None)
  published=[]
  def submit(_,work):
   if work['request_id']==self.work['request_id']:raise CoreRpcError(-8,'Request has no current authenticated automatic ticket')
   published.append(work['request_id'])
   if len(published)==3:self.worker.quitting.set()
  self.worker.submit=submit
  thread=threading.Thread(target=flow.publish_reports);thread.start();thread.join(5)
  if thread.is_alive():self.worker.quitting.set();thread.join(2);self.fail('Orphan still blocked valid successors')
  self.assertEqual(len(published),3);self.assertEqual([],self.out._read());self.assertTrue(self.record().is_file())

if __name__=='__main__':unittest.main()
