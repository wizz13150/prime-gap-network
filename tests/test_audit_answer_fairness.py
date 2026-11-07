"""Bounded recovery with a large withheld-owner backlog; no Core/GPU/network."""
import copy
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest

P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'src/protocol'),str(P/'src/engine/runtime_host'),str(P/'tests')]
from horizon_audit_answers import AuditAnswers
from horizon_network_audits import NetworkAudits,save
from audit_publication_chain_fixture import ChainRPC


class FairAnswerRecovery(unittest.TestCase):
 def sealed(self,folder,n):
  name=f'{n:064x}-0-0';value=dict(request_id=f'{n:064x}',slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64)))
  (folder/(name+'.json')).write_text(json.dumps(value),encoding='utf8');return name

 def test_later_ready_answer_survives_5000_waiting_entries_and_cold_restarts(self):
  with tempfile.TemporaryDirectory(prefix='pgn-fair-audit-')as temp:
   worker=SimpleNamespace(root=Path(temp),rpc=ChainRPC());audit=NetworkAudits(worker)
   names=[self.sealed(audit.folder,n)for n in range(1,5001)]
   for offset in range(0,len(names),1000):audit.answers.register(names[offset:offset+1000])
   with audit.answers.database()as db:db.execute("INSERT OR REPLACE INTO meta VALUES('migration_done','true')")
   save(audit.folder/'pending.json',names[:4096]);target=names[-1][:64];published=[];largest=0
   answer=audit.folder/(names[-1]+'.json');original=hashlib.sha256(answer.read_bytes()).hexdigest()
   waiting=dict(jobs=[dict(slot=0,attempt=0,state='committed')])
   ready=dict(waiting,details_root='c'*64)
   passed=copy.deepcopy(ready);passed['jobs'][0]['state']='passed'
   def bind(instance):
    def states(requests):
     nonlocal largest
     requests=list(requests);largest=max(largest,len(requests))
     self.assertLessEqual(len(requests),64,'Old waiting index monopolizes the active recovery window')
     return {r:copy.deepcopy(ready if r==target else waiting)for r in requests}
    instance.states=states;instance.state=lambda request:copy.deepcopy(passed)
    instance.apply=lambda *args:(published.append(args)or dict(available=True))
   bind(audit)
   for turn in range(81):
    audit.reveal_committed()
    self.assertLessEqual(len(json.loads((audit.folder/'pending.json').read_bytes())),64)
    if published:break
    if turn%13==12:audit=NetworkAudits(worker);bind(audit)
   self.assertEqual(len(published),1,'Ready answer starved behind withheld owner reports')
   self.assertEqual(published[0][1],target);self.assertLessEqual(largest,64)
   self.assertEqual(hashlib.sha256(answer.read_bytes()).hexdigest(),original)
   with audit.answers.database()as db:
    self.assertEqual(db.execute('SELECT count(*) FROM answers').fetchone()[0],5000)
    self.assertEqual(db.execute('SELECT count(*) FROM answers WHERE waiting=1').fetchone()[0],4999)
   self.assertEqual(len(list(audit.folder.glob('*-0-0.json'))),5000)

 def test_corrupt_answer_cannot_advance_fair_cursor(self):
  with tempfile.TemporaryDirectory()as temp:
   folder=Path(temp);catalog=AuditAnswers(folder);names=[self.sealed(folder,n)for n in(1,2)]
   catalog.register(names);self.assertEqual([n for n,_ in catalog.next_page(1)],[names[0]])
   path=folder/(names[1]+'.json');original=path.read_bytes();value=json.loads(original);value['answer']['body']['counts']=['99'];path.write_text(json.dumps(value),encoding='utf8')
   with self.assertRaisesRegex(ValueError,'digest'):catalog.next_page(1)
   path.write_bytes(original);self.assertEqual([n for n,_ in AuditAnswers(folder).next_page(1)],[names[1]])

 def test_page_budget_and_corrupt_cursor_fail_closed(self):
  with tempfile.TemporaryDirectory()as temp:
   folder=Path(temp);catalog=AuditAnswers(folder);name=self.sealed(folder,1);catalog.register([name])
   for limit in(0,True,65):
    with self.assertRaises(ValueError):catalog.next_page(limit)
   with catalog.database()as db:db.execute("INSERT INTO meta VALUES('waiting_cursor','../invalid')")
   with self.assertRaisesRegex(ValueError,'cursor'):catalog.next_page()
   self.assertTrue((folder/(name+'.json')).is_file())


if __name__=='__main__':unittest.main()
