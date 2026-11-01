import pathlib,sys,unittest
from types import SimpleNamespace
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from pgn_plan import choose_plan
from pgn_resident import Manager

class ResidentProfileTests(unittest.TestCase):
 def test_warm_fitting_profile_avoids_destructive_other_probe(self):
  calls=[]
  def query(worker,start,length):
   calls.append(worker);self.assertEqual(worker,'k32');return dict(profile_k=32,range_valid=True,fits=True)
  result=choose_plan([(64,'k64'),(32,'k32'),(16,'k16')],1000,100,query,preferred_worker='k32')
  self.assertEqual(result['profile_k'],32);self.assertEqual(calls,['k32'])
 def test_cold_policy_still_tries_largest_profile(self):
  calls=[]
  def query(worker,start,length):
   calls.append(worker);return dict(profile_k=int(worker[1:]),range_valid=True,fits=worker=='k32')
  self.assertEqual(choose_plan([(32,'k32'),(64,'k64')],1000,100,query)['profile_k'],32);self.assertEqual(calls,['k64','k32'])
 def test_warm_profile_that_no_longer_fits_is_not_forced(self):
  calls=[]
  def query(worker,start,length):
   calls.append(worker);return dict(profile_k=int(worker[1:]),range_valid=True,fits=worker=='k16',max_length_fit=0)
  self.assertEqual(choose_plan([(64,'k64'),(32,'k32'),(16,'k16')],1000,100,query,preferred_worker='k32')['profile_k'],16);self.assertEqual(calls,['k32','k64','k16'])
 def test_unknown_warm_image_refused(self):
  with self.assertRaises(ValueError):choose_plan([(32,'k32')],1000,100,lambda *args:None,preferred_worker='foreign')
 def test_actual_resident_liveness_required(self):
  manager=Manager(None,'.','gpu',None);self.assertIsNone(manager.active_worker())
  manager.resident=SimpleNamespace(_closed=False,command=['k32'],owner=SimpleNamespace(poll=lambda:None));self.assertEqual(manager.active_worker(),'k32')
  manager.resident._closed=True;self.assertIsNone(manager.active_worker());manager.resident._closed=False;manager.resident.owner.poll=lambda:0;self.assertIsNone(manager.active_worker())
if __name__=='__main__':unittest.main()
