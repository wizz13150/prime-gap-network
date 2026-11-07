from pathlib import Path
from types import SimpleNamespace
import json,sys,tempfile,threading
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'src/engine/runtime_host'),str(P/'src/protocol')]
from horizon_network_audits import NetworkAudits
from report_commitment import digest
class Rpc:
 def __init__(self):self.stage='none';self.committed=None;self.revealed=None;self.queries=[];self.disclosed=False
 def call(self,method,params=None,wallet=False):
  params=params or[];self.queries.append(method)
  if method=='signmessage':return 'test-signature'
  if method=='getbestblockhash':return '55'*32
  if method=='getblockheader':return dict(height=100)
  if method=='getblockhash':return '55'*32
  if method=='getpgnworkview':return dict(schema='PGN_WORK_VIEW_V1',kind='audits',request_id=params[3],state=dict(genesis='55'*32,height=100,bestblockhash='55'*32),audits=[dict(request_id=params[3],jobs=[dict(slot=0,attempt=0,state=self.stage)])])
  if method=='getpgnauditevent':
   action=params[0]
   if action=='assign'and self.stage!='none':return dict(available=False)
   ranges=[dict(index=i,start=str(150000000000000000000+i*2000000000000),end=str(150000000000000000000+(i+1)*2000000000000))for i in range(5)]
   return dict(available=True,challenge='fixture',envelope=action,request_id='11'*32,slot=0,attempt=0,ranges=ranges)
  if method=='getpgnreports':return [dict(ticket='22'*32,report_root='33'*32)]
  if method=='getpgnscientificwork':return dict(target_numerator='32',target_denominator='1')
  if method=='getpgnauditstate':return [dict(request_id='11'*32,jobs=[dict(slot=0,attempt=0,state=self.stage)],**({'details_root':'44'*32}if self.disclosed else{}))]
  raise AssertionError(method)
class Source:
 def __init__(self):self.jobs=[]
 def run_ticket(self,job,limit):self.jobs.append(job);return dict(body=dict(counts=['1']+['0']*99))
with tempfile.TemporaryDirectory(prefix='pgn-audit-client-')as temporary:
 worker=SimpleNamespace(root=Path(temporary),rpc=Rpc(),source=Source(),args=SimpleNamespace(piece_limit=64000000000000),running=threading.Event(),network_lock=threading.RLock(),address=lambda:'fixture-auditor');worker.running.set();service=NetworkAudits(worker)
 def ledger(envelopes):
  if envelopes==['assign']:worker.rpc.stage='assigned'
  elif envelopes==['commit']:
   saved=json.loads((service.folder/'current.json').read_bytes());worker.rpc.committed=saved['answer']['commitment'];assert (service.folder/'pending.json').exists();worker.rpc.stage='committed'
  elif envelopes==['reveal']:worker.rpc.stage='passed'
  else:raise AssertionError(envelopes)
 worker.ledger=ledger
 assert service.process_one()=='committed';assert len(worker.source.jobs)==5;assert len(set(x['ticket_id']for x in worker.source.jobs))==5
 saved=json.loads(next(p for p in service.folder.glob('*.json')if p.name not in ['current.json','pending.json']).read_bytes());assert digest(saved['answer']['body'])==worker.rpc.committed;assert saved['answer']['body']['counts']==['1']*5
 service.reveal_committed();assert worker.rpc.stage=='committed';assert len(json.loads((service.folder/'pending.json').read_bytes()))==1
 worker.rpc.disclosed=True;service.reveal_committed();assert worker.rpc.stage=='passed'and json.loads((service.folder/'pending.json').read_bytes())==[]
 assert all(not {'expected','counts','best','report_body'} & set(job) for job in worker.source.jobs)
 print(json.dumps(dict(PASS=True,partitions_computed=5,distinct_job_identity=True,commitment_precedes_reveal=True,delayed_owner_disclosure=True,no_expected_counts_input=True,scope='Closed client/protocol fixture with fake computation; no independent GPU or live acceptance claim')))

# A full active window must not prevent committing a newly computed answer.
# The catalogue is its recovery authority before any network publication.
with tempfile.TemporaryDirectory(prefix='pgn-full-audit-window-')as temporary:
 worker=SimpleNamespace(root=Path(temporary),rpc=Rpc(),source=Source(),args=SimpleNamespace(piece_limit=64000000000000),running=threading.Event(),network_lock=threading.RLock(),address=lambda:'fixture-auditor');worker.running.set();service=NetworkAudits(worker)
 old=[]
 for n in range(64):
  request=f'{(1<<255)+n:064x}';name=request+'-0-0';old.append(name)
  (service.folder/(name+'.json')).write_text(json.dumps(dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64)))),encoding='utf8')
 from horizon_network_audits import save
 save(service.folder/'pending.json',old)
 def full_ledger(envelopes):
  if envelopes==['assign']:worker.rpc.stage='assigned'
  elif envelopes==['commit']:
   saved=json.loads((service.folder/'current.json').read_bytes());worker.rpc.committed=saved['answer']['commitment']
   with service.answers.database()as db:
    assert db.execute('SELECT waiting FROM answers WHERE name=?',('11'*32+'-0-0',)).fetchone()==(1,),'Commit sent before independent durable recovery'
   worker.rpc.stage='committed'
  elif envelopes==['reveal']:worker.rpc.stage='passed'
  else:raise AssertionError(envelopes)
 worker.ledger=full_ledger
 assert service.process_one()=='committed'
 assert json.loads((service.folder/'pending.json').read_bytes())==old
 worker.rpc.disclosed=True
 for _ in range(3):
  service.reveal_committed()
  if worker.rpc.stage=='passed':break
 assert worker.rpc.stage=='passed'
 assert len(json.loads((service.folder/'pending.json').read_bytes()))<=64
 assert all((service.folder/(name+'.json')).is_file()for name in old)
 print(json.dumps(dict(PASS=True,full_active_window=64,new_answer_committed=True,new_answer_catalogued_before_send=True,later_answer_revealed=True,old_answers_retained=True,GPU_used=False)))
