"""Regression: a newly prepared miner must be OFF without invented work."""
from pathlib import Path
import contextlib,io,json,sys,tempfile,unittest
HOST=Path(__file__).resolve().parents[1]/'src/engine/runtime_host'
sys.path.insert(0,str(HOST))
import horizon_status_stream as stream

class FreshStatusTests(unittest.TestCase):
 def observe(self,root):
  with contextlib.redirect_stdout(io.StringIO())as output:
   from unittest.mock import patch
   with patch.object(sys,'argv',['host','--observe','--state-root',str(root),'--once']):self.assertEqual(stream.main(),0)
  return json.loads(output.getvalue())
 def test_first_install_absent_folder_is_off_and_remains_absent(self):
  with tempfile.TemporaryDirectory()as d:
   root=Path(d)/'never-started';v=self.observe(root)
   self.assertEqual(v['phase'],'Paused');self.assertFalse(root.exists());self.assertFalse(v['mining_started']);self.assertFalse(v['allocation_requested'])
   for name in ('ticket_id','positions','speed_rate','tests','request_id'):self.assertNotIn(name,v)
 def test_empty_prepared_directory_is_off_without_writes(self):
  with tempfile.TemporaryDirectory()as d:
   self.assertEqual(self.observe(Path(d))['phase'],'Paused');self.assertEqual(list(Path(d).iterdir()),[])
 def test_lost_existing_controller_is_an_error_not_a_fresh_miner(self):
  with tempfile.TemporaryDirectory()as d:
   (Path(d)/'tickets').mkdir();v=self.observe(Path(d));self.assertEqual(v['schema'],'PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1');self.assertIn('nonempty',v['message'])
 def test_corrupt_controller_still_refused(self):
  with tempfile.TemporaryDirectory()as d:
   f=Path(d)/'controller.json';f.write_text('{');self.assertEqual(self.observe(Path(d))['schema'],'PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1');self.assertEqual(f.read_text(),'{')
 def test_missing_ticket_not_promoted_to_ready(self):
  with tempfile.TemporaryDirectory()as d:
   (Path(d)/'controller.json').write_text('{}');self.assertIn('No issued network ticket',self.observe(Path(d))['message'])
 def test_oversized_controller_is_still_refused(self):
  with tempfile.TemporaryDirectory()as d:
   (Path(d)/'controller.json').write_bytes(b' '*262145);self.assertIn('exceeds budget',self.observe(Path(d))['message'])
if __name__=='__main__':unittest.main()
