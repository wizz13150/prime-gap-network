import os,sys,time,unittest,subprocess,gc
from pathlib import Path
from unittest.mock import patch
from types import SimpleNamespace
sys.path.insert(0,str(Path(__file__).resolve().parent))
from runtime_metrics import PositionRateWindow,CpuMetrics,process_reading,host_memory
class RateTests(unittest.TestCase):
 def test_start_and_running(self):
  r=PositionRateWindow();self.assertIsNone(r.observe(0,True,0));self.assertAlmostEqual(r.observe(250,True,.25),1000);self.assertAlmostEqual(r.observe(1000,True,1),1000)
 def test_confirmed_idle_is_zero_not_unknown(self):
  r=PositionRateWindow();r.observe(0,True,0);r.observe(1000,True,1)
  self.assertEqual(r.observe(1000,False,2),0);self.assertEqual(r.observe(1000,False,3),0);self.assertEqual(r.observe(1250,True,3.25),1000)
 def test_backward_prefix_and_clock_restart(self):
  r=PositionRateWindow();r.observe(1000,True,2);self.assertIsNone(r.observe(10,True,3));self.assertIsNone(r.observe(20,True,1))
 def test_duplicate_clock_and_window_bound(self):
  r=PositionRateWindow();r.observe(0,True,0);self.assertIsNone(r.observe(100,True,0))
  for i in range(1,101):r.observe(i*1000,True,i)
  self.assertLessEqual(len(r.samples),7);self.assertEqual(r.observe(101000,True,101),1000)
class MetricsTests(unittest.TestCase):
 def test_idle_real_memory_without_workers(self):
  total,used=host_memory();self.assertGreater(total,0);self.assertGreaterEqual(used,0);self.assertLessEqual(used,total)
  cpu,rss=process_reading(os.getpid());self.assertGreaterEqual(cpu,0);self.assertGreater(rss,0)
  m=CpuMetrics();v=m.sample([],4);self.assertEqual(v['worker_busy_percent'],0);self.assertGreater(int(v['engine_working_set_bytes']),0);self.assertEqual(v['worker_pids'],[])
 def test_owned_worker_utilization_and_memory(self):
  m=CpuMetrics();w=SimpleNamespace(p=SimpleNamespace(pid=123))
  with patch('runtime_metrics.process_reading',side_effect=[(0,10),(1,20),(0,10),(2,20)]),patch('runtime_metrics.host_memory',return_value=(1000,500)):
   first=m.sample([w],1,0);second=m.sample([w],1,1)
  self.assertIsNone(first['worker_busy_percent']);self.assertEqual(second['worker_busy_percent'],100);self.assertEqual(second['engine_working_set_bytes'],'30')
 def test_failed_optional_read_does_not_abort(self):
  m=CpuMetrics();w=SimpleNamespace(p=SimpleNamespace(pid=123))
  with patch('runtime_metrics.process_reading',side_effect=OSError('not available')),patch('runtime_metrics.host_memory',side_effect=OSError('not available')):
   v=m.sample([w],1,0)
  self.assertIsNone(v['worker_busy_percent']);self.assertIsNone(v['engine_working_set_bytes']);self.assertIsNone(v['host_memory_total_bytes']);self.assertTrue(v['error'])
 @unittest.skipUnless(os.name=='nt','Windows ctypes retention regression')
 def test_repeated_real_sampling_keeps_memory_bounded(self):
  from native_transport import Load
  meter=CpuMetrics();load=Load()
  for i in range(100):meter.sample([],4,1000+i*.3);load.sample()
  gc.collect();before=process_reading(os.getpid())[1]
  for i in range(2000):meter.sample([],4,2000+i*.3);load.sample()
  gc.collect();after=process_reading(os.getpid())[1]
  self.assertLess(after-before,8*1024*1024)
 def test_metrics_throttled_but_worker_change_is_immediate(self):
  m=CpuMetrics();w=SimpleNamespace(p=SimpleNamespace(pid=123))
  with patch('runtime_metrics.process_reading',return_value=(1,10))as read,patch('runtime_metrics.host_memory',return_value=(1000,500)):
   a=m.sample([],4,0);self.assertEqual(m.sample([],4,.1),a);self.assertEqual(read.call_count,1)
   m.sample([w],4,.1);self.assertEqual(read.call_count,3)
if __name__=='__main__':unittest.main()
