"""A retained bad seal must not strand unrelated valid publications."""
from pathlib import Path
from types import SimpleNamespace
import copy,json,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol'),str(ROOT/'tests')]
from horizon_network_audits import NetworkAudits,save
from horizon_report_poll import ReportPoller
from audit_publication_chain_fixture import ChainRPC

class PublicationIsolation(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
  self.worker=SimpleNamespace(root=Path(self.temp.name),rpc=ChainRPC())
  self.audit=NetworkAudits(self.worker);self.names=[];self.original={}
  for n in (1,2):
   request=f'{n:064x}';name=request+'-0-0';self.names.append(name)
   value=dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64)))
   save(self.audit.folder/(name+'.json'),value);self.original[name]=(self.audit.folder/(name+'.json')).read_bytes()
  self.audit.answers.register(self.names)
  with self.audit.answers.database()as db:db.execute("INSERT OR REPLACE INTO meta VALUES('migration_done','true')")
  save(self.audit.folder/'pending.json',self.names)
  path=self.audit.folder/(self.names[0]+'.json');bad=json.loads(path.read_bytes());bad['answer']['body']['counts']=['99'];save(path,bad)
  self.corrupt=path.read_bytes();self.sent=[]
  self.bind(self.audit)
 def bind(self,audit):
  ready=dict(details_root='c'*64,jobs=[dict(slot=0,attempt=0,state='committed')])
  audit.states=lambda requests:{r:copy.deepcopy(ready)for r in requests}
  done=copy.deepcopy(ready);done['jobs'][0]['state']='passed'
  audit.state=lambda request:copy.deepcopy(done)
  audit.apply=lambda *args:(self.sent.append(args)or dict(available=True))
 def waiting(self,audit):
  with audit.answers.database()as db:return dict(db.execute('SELECT name,waiting FROM answers'))
 def test_bad_seal_is_retained_while_healthy_answer_finalizes(self):
  self.audit.reveal_committed()
  self.assertEqual([v[1]for v in self.sent],[self.names[1][:64]])
  self.assertEqual(self.waiting(self.audit),{self.names[0]:1,self.names[1]:0})
  self.assertEqual((self.audit.folder/(self.names[0]+'.json')).read_bytes(),self.corrupt)
  self.assertEqual((self.audit.folder/(self.names[1]+'.json')).read_bytes(),self.original[self.names[1]])
  self.assertEqual(len(self.audit.answers.publication_issues),1)
 def test_restart_and_repair_reuse_original_seal_without_duplicate_publication(self):
  self.audit.reveal_committed()
  reloaded=NetworkAudits(self.worker);self.bind(reloaded);reloaded.reveal_committed()
  self.assertEqual(len(self.sent),1)
  (reloaded.folder/(self.names[0]+'.json')).write_bytes(self.original[self.names[0]])
  reloaded.reveal_committed()
  self.assertEqual([v[1]for v in self.sent],[self.names[1][:64],self.names[0][:64]])
  self.assertTrue(all(v==0 for v in self.waiting(reloaded).values()))
  self.assertEqual(reloaded.answers.publication_issues,())
 def test_partial_progress_still_reports_the_retained_error(self):
  owner=SimpleNamespace(reveal_owner_reports=lambda:None,reveal_committed=lambda:None)
  poll=ReportPoller(owner,[self.audit]);error=poll.poll_audits()
  self.assertEqual(len(self.sent),1)
  self.assertIsNotNone(error)
  self.assertIn('retained',error.lower())
  self.assertIn('recovery',error.lower())

if __name__=='__main__':unittest.main()

