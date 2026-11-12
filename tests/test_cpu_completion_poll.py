from pathlib import Path
import sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_cpu_source import completion_poll_delay
class CompletionPollTests(unittest.TestCase):
 def test_only_near_completion_uses_short_wait(self):
  s=dict(result_resident_policy=True,compute_started=True,total_positions='20000000000',completed_positions='19500000000',positions_per_second=2e9)
  self.assertEqual(completion_poll_delay(s),.02)
  s['completed_positions']='10000000000';self.assertEqual(completion_poll_delay(s),.2)
  s['completed_positions']='20000000000';self.assertEqual(completion_poll_delay(s),.2)
 def test_unknown_stopped_or_legacy_cannot_spin(self):
  for s in ({},{'result_resident_policy':False},{'result_resident_policy':True,'compute_started':False},{'result_resident_policy':True,'compute_started':True,'positions_per_second':float('nan')}):self.assertEqual(completion_poll_delay(s),.2)
if __name__=='__main__':unittest.main()
