import pathlib,sys,unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from service import ControlLease,Service

class ControlLeaseTests(unittest.TestCase):
 def setUp(self):self.now=0.;self.lease=ControlLease(45,5,lambda:self.now)
 def test_idle_pipe_cannot_hold_workspace_forever(self):
  self.now=44.999;self.assertFalse(self.lease.close_due())
  self.now=45;self.assertTrue(self.lease.close_due());self.assertFalse(self.lease.drain_expired())
  self.now=49.999;self.assertFalse(self.lease.drain_expired())
  self.now=50;self.assertTrue(self.lease.drain_expired())
 def test_live_control_renews_without_additional_messages(self):
  for i in range(1,100):
   self.now=i*44;self.assertFalse(self.lease.close_due());self.lease.observe()
  self.now+=45;self.assertTrue(self.lease.close_due())
 def test_eof_closes_immediately_and_late_command_cannot_revive_it(self):
  self.now=1;self.assertTrue(self.lease.close_due(eof=True))
  self.now=2;self.lease.observe();self.assertEqual(self.lease.closing_since,1)
  self.now=6;self.assertTrue(self.lease.close_due());self.assertTrue(self.lease.drain_expired())
 def test_invalid_limits_rejected(self):
  for timeout,grace in[(0,5),(-1,5),(45,-1)]:
   with self.assertRaises(ValueError):ControlLease(timeout,grace)

class BudgetScaleTests(unittest.TestCase):
 def service(self,system=12,background=21):
  s=Service.__new__(Service);s.threads=2;s.inv=dict(available_cpus=[2,4,6],logical_cpus_system=system);s.load=SimpleNamespace(reset=lambda:None,update=lambda:background);return s
 def test_restricted_affinity_keeps_machine_load_units(self):
  s=self.service();self.assertAlmostEqual(s.estimated_worker_load(),100/6)
  with patch('service.time.sleep'):self.assertTrue(s.admit_budget())
  self.assertEqual(s.inv['available_cpus'],[2,4,6])
 def test_real_machine_budget_still_refuses_excess_load(self):
  with patch('service.time.sleep'):self.assertFalse(self.service(background=70).admit_budget())
 def test_unknown_invalid_and_multigroup_inventory_stays_conservative(self):
  for value in[None,True,0,2,128]:
   s=self.service(system=value);self.assertAlmostEqual(s.estimated_worker_load(),200/3)
   with patch('service.time.sleep'):self.assertFalse(s.admit_budget())

if __name__=='__main__':unittest.main()
