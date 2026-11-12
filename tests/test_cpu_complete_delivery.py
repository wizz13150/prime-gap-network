"""Whole-ticket delivery conserves records beyond the display's best100."""
from pathlib import Path
import copy,sys,unittest
sys.path.insert(0,str(Path(__file__).resolve().parent))
from test_cpu_only_source import CpuSourceTests,Endpoint,ticket,context,cycle
from horizon_cpu_cycle import digest

class CompleteEndpoint(Endpoint):
 def __init__(self):super().__init__();self.executions=[];self.drop_last=False;self.bad_bins=False
 def status(self):return dict(super().status(),complete_record_delivery='PGN_CPU_COMPLETE_RECORDS_V1',resident_result_handoff=True)
 def records(self):
  lo=int(self.request['start']);return[dict(lower_prime=str(lo+1+2000*i),upper_prime=str(lo+1001+2000*i),gap=1000)for i in range(150)]
 def call(self,action,**kw):
  if action=='execute':self.executions.append(copy.deepcopy(kw['request']))
  if action=='evidence':
   rows=self.records();counts=[0]*100;lo=int(self.request['start']);length=int(self.request['end'])-lo
   for r in rows:counts[(int(r['lower_prime'])-lo)*100//length]+=1
   if self.bad_bins:counts[0],counts[-1]=counts[-1],counts[0]
   return dict(evidence=dict(request=self.request,request_sha256=digest(self.request),complete=True,counts100=list(map(str,counts)),best=rows[:100],candidate_tests='1000'))
  if action=='records':
   assert kw['request_sha256']==digest(self.request)
   rows=[r for r in self.records()if not kw['after']or int(r['lower_prime'])>int(kw['after'])];rows=rows[:-1]if self.drop_last else rows
   page=rows[:kw['limit']]
   return dict(records=dict(schema='PGN_CPU_COMPLETE_RECORDS_V1',request_sha256=digest(self.request),total_records='150',records=page,next_after=page[-1]['lower_prime']if len(rows)>len(page)else None))
  return super().call(action,**kw)

class CompleteDeliveryTests(CpuSourceTests):
 # Inherit the established pause/ACK/ownership tests; new assertions below
 # exercise the capability path rather than weakening legacy compatibility.
 def whole(self):
  self.endpoint=CompleteEndpoint();s=self.open();t=ticket();t['end']=str(int(t['start'])+1000000)
  from pgn_native_contract import fields
  t['threshold_policy']=fields(int(t['start']),int(t['end']))['threshold_policy'];s.bind_cycle(t,context(),cycle());return s,t
 def test_whole_request_has_no20B_or_maximum_piece_delivery_boundary(self):
  s,t=self.whole();r=s.run_ticket(t,10000)
  self.assertEqual(self.endpoint.executions,[s.grant]);self.assertEqual(self.endpoint.acks,1)
  self.assertEqual(sum(map(int,r['body']['counts'])),150);self.assertEqual(len(r['body']['best']),100)
 def test_missing_final_page_records_stay_unacknowledged(self):
  s,t=self.whole();self.endpoint.drop_last=True
  with self.assertRaisesRegex(ValueError,'complete primary'):s.run_ticket(t,10000)
  self.assertEqual(self.endpoint.acks,0);self.assertIsNotNone(self.endpoint.request)
 def test_same_total_with_wrong_partition_counts_cannot_be_acknowledged(self):
  s,t=self.whole();self.endpoint.bad_bins=True
  with self.assertRaisesRegex(ValueError,'exact partition counts'):s.run_ticket(t,10000)
  self.assertEqual(self.endpoint.acks,0)
if __name__=='__main__':unittest.main()
