import importlib.util,pathlib,unittest
p=pathlib.Path(__file__).resolve().parents[1]/'src/engine/cpu_auditor/shared_service/runtime_metrics.py'
spec=importlib.util.spec_from_file_location('cpu_science_metrics',p);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m)
class MiningRates(unittest.TestCase):
 def test_same_window_means_and_yield(self):
  w=m.MiningCounterWindow();self.assertIsNone(w.observe('a',0.,0,0,0,True)['tests_per_second'])
  r=w.observe('a',1.,2_000_000_000,4_000_000,6,True);self.assertEqual((r['tests_per_second'],r['gaps_per_second'],r['gaps_per_billion_positions']),(4_000_000.,6.,3.))
  r=w.observe('a',2.,4_000_000_000,10_000_000,10,True);self.assertEqual((r['tests_per_second'],r['gaps_per_second'],r['gaps_per_billion_positions']),(5_000_000.,5.,2.5))
 def test_pause_keeps_active_clock_and_never_relabels_idle_as_yield(self):
  w=m.MiningCounterWindow();w.observe('a',0.,0,0,0,True);w.observe('a',1.,1000,500,2,True)
  r=w.observe('a',1.,1000,500,2,False);self.assertEqual(r['tests_per_second'],0.);self.assertEqual(r['gaps_per_second'],0.);self.assertIsNone(r['gaps_per_billion_positions'])
  r=w.observe('a',2.,2000,1000,4,True);self.assertEqual(r['active_window_seconds'],2.);self.assertEqual(r['tests_per_second'],500.)
 def test_new_work_and_counter_rewind_do_not_mix_namespaces(self):
  w=m.MiningCounterWindow();w.observe('a',0.,0,0,0,True);w.observe('a',1.,1000,500,2,True)
  self.assertIsNone(w.observe('b',0.,0,0,0,True)['tests_per_second']);self.assertIsNone(w.observe('b',1.,0,0,0,True)['gaps_per_billion_positions'])
  w.observe('b',2.,1000,500,2,True);self.assertIsNone(w.observe('b',3.,500,250,1,True)['tests_per_second'])
 def test_arbitrary_precision_deltas(self):
  w=m.MiningCounterWindow();v=2**95;w.observe('a',0.,v,v//4,100000,True);r=w.observe('a',1.,v+1000,v//4+100,100002,True)
  self.assertEqual(r['tests_per_second'],100.);self.assertEqual(r['gaps_per_billion_positions'],2_000_000.)
 def test_late_sample_starts_fresh_and_memory_stays_bounded(self):
  w=m.MiningCounterWindow();w.observe('a',0.,0,0,0,True);self.assertIsNone(w.observe('a',100.,1000,500,2,True)['tests_per_second'])
  for i in range(5000):w.observe('a',101.+i*.001,1000+i,500+i,2,True)
  self.assertLessEqual(len(w.samples),1024)
 def test_invalid_input_is_not_a_plausible_rate(self):
  for t,p,q,g in [(float('nan'),0,0,0),(-1.,0,0,0),(0.,-1,0,0),(0.,0,1.5,0)]:
   with self.assertRaises(ValueError):m.MiningCounterWindow().observe('a',t,p,q,g,True)
if __name__=='__main__':unittest.main()
