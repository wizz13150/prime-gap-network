from pathlib import Path
import copy,sys,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from horizon_testnet_admission import join_status,AdmissionMonitor
G='a'*64;P='b'*64;R='c'*64;O='d'*64;A='0014'+'11'*20;M='0014'+'22'*20
class Rpc:
 def __init__(self):
  self.registered=False;self.enabled=False;self.operator=O;self.count=3;self.tip=P;self.foreign=False;self.wrong_script=False;self.owner=True;self.lease=True;self.changed_parent=False;self.confirmed=True;self.submits=0;self.calls=[];self.metrics_schema='PGN_NETWORK_METRICS_V1'
 def call(self,method,args=None,wallet=False):
  args=args or [];self.calls.append(method)
  if method=='validateaddress':return dict(isvalid=True,scriptPubKey=A if args[0]=='authority'else M)
  if method=='getpgnparticipants':return dict(genesis=G,bestblockhash=self.tip,authority_script=A,authority_request=R,participants=[dict(signing_script=A if self.wrong_script else M,operator_id=self.operator,may_audit=self.enabled)]if self.registered else[])
  if method=='getpgnnetworkmetrics':return dict(schema=self.metrics_schema,genesis='f'*64 if self.foreign else G,bestblockhash='e'*64 if self.changed_parent else self.tip,audit_enabled_operators=self.count)
  if method=='getbestblockhash':return self.tip
  if method=='getaddressinfo':return dict(ismine=self.owner,scriptPubKey=A)
  if method=='getpgnstate':return dict(genesis=G,bestblockhash=self.tip,leases=[dict(request=R,miner_script=M)]if self.lease else[])
  if method=='getpgnauditevent':return dict(available=True,genesis=G,parent=self.tip,challenge='fixture',envelope='00')
  if method=='signmessage':return 'fixture-signature'
  if method=='getblockheader':return dict(confirmations=1 if self.confirmed else -1)
  raise AssertionError(method)
 def submit(self,envelopes):
  self.submits+=1;self.registered=True;self.enabled=True;return dict(block_hash='f'*64)
 def cancel_all(self):self.cancelled=True
class AdmissionTests(unittest.TestCase):
 def setUp(self):self.rpc=Rpc()
 def test_open_testnet_allows_new_identity_without_registration_or_diagnostics(self):
  original=self.rpc.call
  def call(method,args=None,wallet=False):
   value=original(method,args,wallet)
   if method=='getpgnparticipants':value['admission_mode']='open'
   return value
  self.rpc.call=call;self.rpc.count=0
  s=join_status(self.rpc,G,'miner')
  self.assertFalse(s['registered']);self.assertTrue(s['normal_cycle_permission']);self.assertTrue(s['audit_permission']);self.assertEqual(s['blockers'],[])
  self.assertFalse(s['own_report_membership_quorum']);self.assertEqual(self.rpc.submits,0)
 def test_unknown_admission_mode_is_not_silently_interpreted_as_open(self):
  original=self.rpc.call
  def call(method,args=None,wallet=False):
   value=original(method,args,wallet)
   if method=='getpgnparticipants':value['admission_mode']='unknown'
   return value
  self.rpc.call=call
  with self.assertRaisesRegex(ValueError,'admission policy'):join_status(self.rpc,G,'miner')
 def test_registered_permission_and_two_other_groups(self):
  self.rpc.registered=self.rpc.enabled=True;s=join_status(self.rpc,G,'miner');self.assertEqual(s['independent_registered_operators'],2);self.assertTrue(s['own_report_membership_quorum'])
 def test_activated_single_operator_status_does_not_invent_independence(self):
  self.rpc.registered=self.rpc.enabled=True;self.rpc.count=1;original=self.rpc.call
  def call(method,args=None,wallet=False):
   value=original(method,args,wallet)
   if method=='getpgnnetworkmetrics':value['height']=1000;value['audit_operator_policy']=dict(schema='PGN_AUDIT_OPERATOR_POLICY_V1',genesis=G,self_rechecks_permitted=True,required_distinct_other_operators=0,activation_height=1001)
   return value
  self.rpc.call=call;value=join_status(self.rpc,G,'miner');self.assertTrue(value['self_rechecks_permitted']);self.assertTrue(value['own_report_membership_quorum']);self.assertEqual(value['independent_registered_operators'],0);self.assertEqual(value['required_independent_operators'],0);self.assertEqual(value['blockers'],[])
 def test_foreign_or_inconsistent_policy_is_rejected(self):
  for change in (dict(genesis='f'*64),dict(activation_height=1002),dict(required_distinct_other_operators=2),dict(self_rechecks_permitted=1)):
   self.rpc=Rpc();original=self.rpc.call
   def call(method,args=None,wallet=False):
    value=original(method,args,wallet)
    if method=='getpgnnetworkmetrics':value['height']=1000;value['audit_operator_policy']=dict(dict(schema='PGN_AUDIT_OPERATOR_POLICY_V1',genesis=G,self_rechecks_permitted=True,required_distinct_other_operators=0,activation_height=1001),**change)
    return value
   self.rpc.call=call
   with self.assertRaises(ValueError):join_status(self.rpc,G,'miner')
 def test_alias_addresses_cannot_be_counted_as_independent_operators(self):
  self.rpc.registered=self.rpc.enabled=True;self.rpc.count=1;s=join_status(self.rpc,G,'miner');self.assertEqual(s['independent_registered_operators'],0);self.assertFalse(s['own_report_membership_quorum'])
 def test_missing_registration_is_explicit_not_empty_work(self):
  s=join_status(self.rpc,G,'miner');self.assertFalse(s['normal_cycle_permission']);self.assertIn('registration_required',s['blockers']);self.assertIsNone(s['independent_registered_operators']);self.assertEqual(s['signing_address'],'miner')
 def test_disabled_permission_does_not_guess_foreign_group_count(self):
  self.rpc.registered=True;s=join_status(self.rpc,G,'miner');self.assertIsNone(s['independent_registered_operators']);self.assertIn('audit_permission_required',s['blockers'])
 def test_changing_snapshot_has_bounded_read_retries(self):
  self.rpc.changed_parent=True
  with self.assertRaises(RuntimeError):join_status(self.rpc,G,'miner')
  self.assertEqual(self.rpc.calls.count('getpgnparticipants'),3)
 def test_foreign_genesis_rejected(self):
  self.rpc.foreign=True
  with self.assertRaises(ValueError):join_status(self.rpc,G,'miner')
 def test_wrong_identity_row_rejected(self):
  self.rpc.registered=True;self.rpc.wrong_script=True
  with self.assertRaises(ValueError):join_status(self.rpc,G,'miner')
 def test_invalid_schema_and_boolean_count_rejected(self):
  self.rpc.metrics_schema='wrong'
  with self.assertRaises(ValueError):join_status(self.rpc,G,'miner')
  self.rpc.metrics_schema='PGN_NETWORK_METRICS_V1';self.rpc.count=True
  with self.assertRaises(ValueError):join_status(self.rpc,G,'miner')
class AdmissionObservationTests(unittest.TestCase):
 def setUp(self):
  self.rpc=Rpc();self.rpc.registered=self.rpc.enabled=True;self.now=10.
  self.monitor=AdmissionMonitor(self.rpc,G,'miner',clock=lambda:self.now)
 def tearDown(self):self.monitor.close()
 def test_presentation_callbacks_never_read_rpc(self):
  self.assertFalse(self.monitor.snapshot()['available']);self.monitor.refresh_once()
  before=len(self.rpc.calls)
  for _ in range(300):self.assertTrue(self.monitor.snapshot()['available'])
  self.assertEqual(len(self.rpc.calls),before);self.assertEqual(self.rpc.submits,0)
 def test_unselected_identity_does_not_create_or_guess_wallet_address(self):
  observer=AdmissionMonitor(self.rpc,G);observer.refresh_once()
  self.assertEqual(observer.snapshot()['state'],'identity_not_selected');self.assertEqual(self.rpc.calls,[])
  observer.close()
 def test_read_failure_is_unknown_not_zero_or_permission(self):
  self.monitor.refresh_once();self.assertTrue(self.monitor.snapshot()['available'])
  self.rpc.foreign=True;self.monitor.refresh_once();s=self.monitor.snapshot()
  self.assertFalse(s['available']);self.assertEqual(s['state'],'unavailable');self.assertNotIn('status',s)
 def test_stale_observation_cannot_grant_permission(self):
  self.monitor.refresh_once();self.now+=91;s=self.monitor.snapshot()
  self.assertEqual(s['state'],'stale');self.assertFalse(s['available']);self.assertNotIn('status',s)
 def test_identity_switch_discards_inflight_old_permission(self):
  call=self.rpc.call;switched=False
  def switching(method,args=None,wallet=False):
   nonlocal switched
   if method=='getpgnparticipants'and not switched:
    switched=True;self.monitor.select_identity('second-miner')
   return call(method,args,wallet)
  self.rpc.call=switching;self.monitor.refresh_once()
  self.assertEqual(self.monitor.snapshot()['state'],'checking');self.assertFalse(self.monitor.snapshot()['available'])
  self.monitor.refresh_once();self.assertTrue(self.monitor.snapshot()['available'])
 def test_returned_snapshot_cannot_mutate_cached_status(self):
  self.rpc.count=1;self.monitor.refresh_once();s=self.monitor.snapshot();s['status']['blockers'].clear();s['status']['normal_cycle_permission']=False
  self.assertIn('more_independent_registered_operators_required',self.monitor.snapshot()['status']['blockers'])
  self.assertTrue(self.monitor.snapshot()['status']['normal_cycle_permission'])
 def test_closed_monitor_cannot_publish_or_restart_observation(self):
  self.monitor.refresh_once();self.monitor.close();self.monitor.select_identity('another');self.monitor.start();self.monitor.refresh_once()
  self.assertEqual(self.monitor.snapshot()['state'],'closed');self.assertFalse(self.monitor.snapshot()['available']);self.assertTrue(self.rpc.cancelled)

class NetworkSummaryTests(unittest.TestCase):
 def setUp(self):
  from horizon_network_summary import summarize
  self.summarize=summarize
  self.metrics=dict(schema='PGN_NETWORK_METRICS_V1',genesis=G,bestblockhash=P,height=500,median_time_past=10000,rate_scope='reported_search_positions_per_chain_second_excludes_audits',audit_validation_scope='historical_reports_qualified_at_snapshot_tip',audit_enabled_operators=3,reporting_operators_in_window=2,audit_queue=dict(awaiting=30,awaiting_seed=2,assigned=3,committed=4),points=[dict(hash=P,height=500,time=10000,window_seconds=600,merit_target_q16=29*65536,reported_positions='2000000000000000',audited_positions='1000000000000000')])
 def test_exact_volume_target_and_backlog_without_online_capacity_claim(self):
  value=self.summarize(self.metrics,G);self.assertTrue(value['available']);self.assertEqual(value['pending_audits'],39);self.assertEqual(value['reported_positions'],'2000000000000000');self.assertEqual(value['merit_target_q16'],29*65536);self.assertIsNone(value['online_miner_count'])
 def test_changed_tip_or_foreign_chain_is_unknown(self):
  for change in [dict(bestblockhash='f'*64),dict(genesis='f'*64)]:
   source=copy.deepcopy(self.metrics);source.update(change);value=self.summarize(source,G);self.assertFalse(value['available']);self.assertNotIn('registered_operators',value)
 def test_invalid_window_count_units_or_boolean_is_unknown(self):
  for key,value in [('window_seconds',0),('window_seconds',86401),('merit_target_q16',True),('merit_target_q16',65*65536)]:
   source=copy.deepcopy(self.metrics);source['points'][-1][key]=value;self.assertFalse(self.summarize(source,G)['available'])
 def test_audited_is_not_larger_than_reported(self):
  self.metrics['points'][-1]['audited_positions']='3000000000000000';self.assertFalse(self.summarize(self.metrics,G)['available'])
 def test_noncanonical_counts_and_unbounded_history_are_unknown(self):
  self.metrics['points'][-1]['reported_positions']='02';self.assertFalse(self.summarize(self.metrics,G)['available']);self.metrics['points']*=257;self.assertFalse(self.summarize(self.metrics,G)['available'])
 def test_missing_metrics_never_become_zero_network_capacity(self):
  value=self.summarize({},G);self.assertFalse(value['available']);self.assertIsNone(value['online_miner_count']);self.assertNotIn('reported_positions',value)
 def test_cached_snapshot_is_deeply_detached_and_performs_no_new_rpc(self):
  rpc=Rpc();rpc.registered=rpc.enabled=True;rpc.count=3;original=rpc.call
  def call(method,args=None,wallet=False):
   if method=='getpgnnetworkmetrics':rpc.calls.append(method);return copy.deepcopy(self.metrics)
   return original(method,args,wallet)
  rpc.call=call;monitor=AdmissionMonitor(rpc,G,'miner');monitor.refresh_once();before=len(rpc.calls)
  snapshot=monitor.snapshot();self.assertTrue(snapshot['status']['network_summary']['available']);snapshot['status']['network_summary']['pending_audits']=999
  self.assertEqual(monitor.snapshot()['status']['network_summary']['pending_audits'],39);self.assertEqual(len(rpc.calls),before);self.assertEqual(rpc.submits,0);monitor.close()

if __name__=='__main__':unittest.main()
