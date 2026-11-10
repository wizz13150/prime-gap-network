"""Actual durable queues/flow with closed, controlled Core transitions."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import copy,hashlib,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol'),str(ROOT/'tests')]
from horizon_report_outbox import ReportOutbox
from horizon_mining_flow import MiningFlow
from horizon_network_audits import NetworkAudits,save
from horizon_work_cycle import WorkCycleJournal
from horizon_audit_recovery import retain_changed_assignment
from report_commitment import digest
from test_work_cycle import work

class PublicationRecoveryTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
 def pair(self,i):
  w=json.loads((ROOT/'tests/fixtures/network/issued-work.json').read_bytes());w.update(ticket=f'{i:064x}',request_id=f'{i+100:064x}')
  result=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_bytes());result['body']['ticket_id']=w['ticket'];result['sha256']=digest(result['body'])
  (self.root/'tickets'/w['ticket']).mkdir(parents=True,exist_ok=True);return w,result
 def test_failed_head_does_not_starve_three_later_reports_or_acknowledge_failure(self):
  out=ReportOutbox(self.root)
  for i in range(1,5):out.put(*self.pair(i))
  original={str(p):p.read_bytes()for p in self.root.rglob('network-report.json')}
  flow=MiningFlow.__new__(MiningFlow);flow.outbox=out;flow.recovered_report=None;flow.finalizer=None;flow.changed=threading.Event();flow.audit_lanes=[]
  flow.cycles=SimpleNamespace(reorg_held=lambda:[]);flow.report_ready=lambda _:True
  flow.worker=SimpleNamespace(root=self.root,quitting=threading.Event(),network_lock=threading.RLock(),audits=SimpleNamespace(reveal_owner_reports=lambda:None,reveal_committed=lambda:None))
  sent=[]
  def submit(_,work):
   sent.append(work['request_id'])
   if work['request_id']==self.pair(1)[0]['request_id']:raise RuntimeError('Request has no current authenticated automatic ticket')
   if len(sent)==4:flow.worker.quitting.set()
  flow.worker.submit=submit
  thread=threading.Thread(target=flow.publish_reports);thread.start();thread.join(6)
  if thread.is_alive():flow.worker.quitting.set();thread.join(2);self.fail('Publication did not advance past failed head')
  self.assertEqual(sent,[self.pair(i)[0]['request_id']for i in range(1,5)])
  self.assertEqual([e['work']['request_id']for e in out._read()],[self.pair(1)[0]['request_id']])
  for name,raw in original.items():self.assertEqual(Path(name).read_bytes(),raw)
 def test_round_robin_cursor_survives_restart_and_never_changes_reports(self):
  out=ReportOutbox(self.root)
  for i in range(1,4):out.put(*self.pair(i))
  raw=out.file.read_bytes();first=out.next_for_publication();again=ReportOutbox(self.root)
  self.assertEqual(again.next_for_publication()['work']['request_id'],self.pair(2)[0]['request_id'])
  self.assertEqual(raw,out.file.read_bytes())
  again.next_for_publication();self.assertEqual(again.next_for_publication(),first)
 def test_actor_transition_uses_real_identity_and_keeps_old_signers_and_files(self):
  for i in range(2):
   p=self.root/'audit-lanes'/str(i)/'audits/current.json';p.parent.mkdir(parents=True);p.write_bytes(b'{"retained":"private existing work"}')
  originals={str(p):p.read_bytes()for p in self.root.rglob('current.json')}
  args=SimpleNamespace(genesis='a'*64,controlled_lab_audit_actors='fixture',pilot_coordination=True)
  def call(method,params):
   self.assertEqual((method,params),('getpgnworkdutystatus',['real-miner']))
   return dict(schema='PGN_NATIVE_WORK_DUTY_STATUS_V1',genesis='a'*64,enforced=True)
  class Worker:pass
  worker=Worker();worker.__dict__.update(root=self.root,args=args,rpc=SimpleNamespace(call=call),source=SimpleNamespace(),sequence=0,
   running=threading.Event(),quitting=threading.Event(),network_lock=threading.RLock(),address=lambda:'real-miner',ledger=lambda *_:None,audits=SimpleNamespace())
  # Allocator is another instance of the real worker class and accepts its fields.
  def journal(*args):
   value=WorkCycleJournal(*args);self.addCleanup(value.close);return value
  with patch('horizon_lab_audit_actors.load_lab_audit_actors',return_value=['lab-one','lab-two']),patch('horizon_mining_flow.WorkCycleJournal',side_effect=journal):flow=MiningFlow(worker)
  self.assertEqual([a.worker.address()for a in flow.audit_lanes],['real-miner','real-miner'])
  self.assertEqual([a.worker.address()for a in flow.legacy_audit_publishers],['lab-one','lab-two'])
  self.assertEqual(len(flow.report_poller.audit_lanes),4)
  self.assertFalse(flow.controlled_lab_audit_actors)
  for name,raw in originals.items():self.assertEqual(Path(name).read_bytes(),raw)
 def test_retirement_preserves_exact_bytes_and_never_grants_confirmation(self):
  folder=self.root/'audit';folder.mkdir();audit=dict(request_id='b'*64,slot=0,attempt=0,counts=[3,4],answer={'private':'retained'})
  source=folder/'current.json';save(source,audit);raw=source.read_bytes();calls=[]
  rpc=SimpleNamespace(call=lambda *_:'a'*64)
  target=retain_changed_assignment(folder,audit,{},'a'*64,rpc,lambda *a:calls.append(a))
  self.assertFalse(source.exists());self.assertEqual(target.read_bytes(),raw);self.assertEqual(len(calls),1)
 def test_moving_chain_or_failed_callback_keeps_current_assignment(self):
  folder=self.root/'audit';folder.mkdir();audit=dict(request_id='b'*64,slot=0,attempt=0,counts=[3]);source=folder/'current.json';save(source,audit);raw=source.read_bytes()
  with self.assertRaises(RuntimeError):retain_changed_assignment(folder,audit,{},'a'*64,SimpleNamespace(call=lambda *_:'c'*64),lambda *_:None)
  self.assertEqual(source.read_bytes(),raw)
  def fail(*_):raise RuntimeError('Injected callback failure')
  with self.assertRaises(RuntimeError):retain_changed_assignment(folder,audit,{},'a'*64,SimpleNamespace(call=lambda *_:'a'*64),fail)
  self.assertEqual(source.read_bytes(),raw)
 def test_changed_assignment_releases_only_unstarted_exact_local_slot(self):
  cycles=WorkCycleJournal(self.root/'cycles',work()['genesis']);self.addCleanup(cycles.close);w=work();cycles.prepare(w)
  cycles.attach(w['request_id'],0,dict(stage='assigned',request='b'*64,slot=0,attempt=0))
  before=cycles.snapshot(w['request_id'])
  with self.assertRaises(ValueError):cycles.retire_assignment(w['request_id'],0,'c'*64,0,0)
  self.assertEqual(cycles.snapshot(w['request_id']),before)
  self.assertTrue(cycles.retire_assignment(w['request_id'],0,'b'*64,0,0));self.assertIsNone(cycles.snapshot(w['request_id'])['audits'][0])
  self.assertFalse(cycles.may_search(w['request_id']))
 def test_real_audit_cursor_retires_stale_attempt_before_requesting_new_work(self):
  rpc=SimpleNamespace(call=lambda *_:'a'*64);w=SimpleNamespace(root=self.root,rpc=rpc)
  retired=[];a=NetworkAudits(w,on_committed=lambda *_:self.fail('Stale result was credited'),on_retired=lambda *args:retired.append(args))
  stale=dict(request_id='b'*64,slot=0,attempt=0,counts=[1,2]);save(a.folder/'current.json',stale);a.state=lambda _:dict(jobs=[])
  self.assertEqual(a.process_one(),'retired');self.assertEqual(len(retired),1)
  self.assertFalse((a.folder/'current.json').exists());self.assertEqual(len(list((a.folder/'retained-assignments').glob('*.json'))),1)

if __name__=='__main__':unittest.main()
