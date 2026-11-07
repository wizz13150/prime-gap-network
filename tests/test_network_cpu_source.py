from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_worker import NetworkWorker
from horizon_cpu_source import CpuOnlyNormalSource

class NetworkCpuSourceTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name)
  self.args=SimpleNamespace(state_root=self.root,bundle=self.root/'no-gpu-installation',cpu_only=True,
   rpc_port=48932,cookie=self.root/'.cookie',genesis='f'*64,wallet='owned-test-wallet',miner=None,
   require_scientific_source=False,import_work=None,legacy_lease=None)
 def test_cpu_host_does_not_load_query_or_lease_a_gpu(self):
  with patch('horizon_network_worker.EngineService',side_effect=AssertionError('GPU engine created')):
   w=NetworkWorker(self.args)
  try:
   self.assertIsInstance(w.source,CpuOnlyNormalSource);w.acquire_device();w.release_device();self.assertIsNone(w.device_lease)
   self.assertIs(w.source.running,w.running);self.assertIs(w.source.stopping,w.quitting)
  finally:w.source.close()
 def test_legacy_checkpoint_is_never_imported_into_cpu_flow(self):
  self.args.import_work=self.root/'legacy.json'
  with self.assertRaisesRegex(ValueError,'legacy GPU'):NetworkWorker(self.args)
  self.assertFalse((self.root/'controller.json').exists())
 def test_cpu_frame_uses_actual_counter_view_without_reading_gpu_session(self):
  w=NetworkWorker(self.args);seen=[];w.emit=seen.append
  try:
   w.current=dict(ticket='a'*64,request_id='b'*64,ticket_number='123')
   w.source.current_frame=dict(schema='PGN_HORIZON_MINING_VIEW_V1',positions='100',speed_rate=4321.5,compute_tests_rate=12.5)
   with patch('horizon_network_worker.ticket_frame',side_effect=AssertionError('GPU journal read')):w._present_progress(dict(phase='SEARCHING'))
   self.assertEqual(seen[0]['title'],'Ticket 123');self.assertEqual(seen[0]['request_id'],'b'*64)
   self.assertEqual(seen[0]['positions'],'100');self.assertEqual(seen[0]['speed_rate'],4321.5)
  finally:w.source.close()
 def test_registered_identity_can_enter_normal_cycle(self):
  w=NetworkWorker(self.args);w.address=lambda:'owned-registered-fixture';w.source.options['CPU/TEST/0']=dict(enabled=True)
  try:
   with patch('horizon_testnet_admission.join_status',return_value=dict(registered=True,normal_cycle_permission=True)):
    self.assertTrue(w.wait_cpu_admission())
  finally:w.source.close()
 def test_unregistered_identity_waits_with_reason_before_any_allocation(self):
  w=NetworkWorker(self.args);w.address=lambda:'owned-unregistered-fixture';w.source.options['CPU/TEST/0']=dict(enabled=True);seen=[]
  def observe(value):
   seen.append(value)
   if value.get('type')=='cpu_notice':w.quitting.set()
  w.emit=observe
  try:
   with patch('horizon_testnet_admission.join_status',return_value=dict(registered=False,normal_cycle_permission=False)):
    self.assertFalse(w.wait_cpu_admission())
   self.assertEqual(seen[-1]['reason_code'],'registration_required')
   self.assertFalse((self.root/'controller.json').exists())
   self.assertFalse((self.root/'prepared-ticket').exists())
  finally:w.source.close()

if __name__=='__main__':unittest.main()
