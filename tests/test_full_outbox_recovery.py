"""A real full durable outbox must not kill cold report recovery."""
from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_mining_flow import MiningFlow
from horizon_report_outbox import ReportOutbox
from report_commitment import digest

class FullOutboxRecoveryTests(unittest.TestCase):
 def test_publisher_drains_four_prior_reports_then_resumed_fifth_without_loss(self):
  raw=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_bytes())
  issued=json.loads((ROOT/'tests/fixtures/network/issued-work.json').read_bytes())
  with tempfile.TemporaryDirectory()as temporary:
   root=Path(temporary);flow=MiningFlow.__new__(MiningFlow);flow.outbox=ReportOutbox(root)
   flow.finalizer=None;flow.changed=threading.Event();flow.audit_lanes=[];advanced=[];sent=[]
   flow.cycles=SimpleNamespace(reorg_held=lambda:[],contains=lambda _:True,advance=lambda *args:advanced.append(args))
   flow.worker=SimpleNamespace(root=root,quitting=threading.Event(),network_lock=threading.RLock(),
      audits=SimpleNamespace(reveal_owner_reports=lambda:None,reveal_committed=lambda:None))
   def pair(i):
    work=dict(issued,ticket=f'{i:064x}',request_id=f'{i+100:064x}')
    (root/'tickets'/work['ticket']).mkdir(parents=True,exist_ok=True)
    result=copy.deepcopy(raw);result['body']['ticket_id']=work['ticket'];result['sha256']=digest(result['body'])
    return work,result
   for i in range(1,5):flow.outbox.put(*pair(i))
   prior={str(p):p.read_bytes()for p in root.rglob('network-report.json')}
   flow.recovered_report=pair(5);flow.report_ready=lambda _:True
   self.assertFalse(flow.drain_recovered_report());self.assertEqual(4,len(flow.outbox._read()))
   self.assertIsNotNone(flow.recovered_report);self.assertFalse(advanced)
   # Auditing stays eligible; new searches wait until the durable recovery
   # has been admitted, rather than overwriting the completed assignment.
   self.assertTrue(flow.may_start_work(SimpleNamespace(context=dict(role='audit',work_cycle=None))))
   self.assertFalse(flow.may_start_work(SimpleNamespace(context=dict(role='search'))))
   def submit(_,work):
    sent.append(work['request_id'])
    if len(sent)==5:flow.worker.quitting.set()
   flow.worker.submit=submit
   thread=threading.Thread(target=flow.publish_reports);thread.start();thread.join(3)
   if thread.is_alive():flow.worker.quitting.set();thread.join(2);self.fail('Full outbox stalled publication')
   self.assertEqual(sent,[pair(i)[0]['request_id']for i in range(1,6)])
   self.assertEqual([],flow.outbox._read());self.assertIsNone(flow.recovered_report)
   self.assertEqual([(pair(5)[0]['request_id'],'completed')],advanced)
   for path,content in prior.items():self.assertEqual(content,Path(path).read_bytes())
   self.assertEqual(5,len(list(root.rglob('network-report.json'))))

if __name__=='__main__':unittest.main()
