from pathlib import Path
import concurrent.futures,json,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from host_runtime_configuration import preserve_bootstrap
G='7'*64
class BootstrapPreservationTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.root=Path(self.temp.name);self.old=self.root/'network';self.new=self.root/'network-staged';self.old.mkdir();self.new.mkdir()
  self.value=dict(schema='PGN_TESTNET_BOOTSTRAP_V1',genesis=G,tls=dict(host='seed.example.org',port=8443),checkpoint=dict(height=1,hash='1'*64));self.raw=json.dumps(self.value,indent=2).encode();(self.old/'bootstrap.json').write_bytes(self.raw)
 def test_code_switch_and_rollback_keep_exact_local_configuration(self):
  (self.old/'.cookie').write_text('private-never-copy');preserve_bootstrap(self.old,self.new,G);self.assertFalse((self.new/'.cookie').exists());backup=self.root/'network-before';self.old.rename(backup);self.new.rename(self.old)
  self.assertEqual((self.old/'bootstrap.json').read_bytes(),self.raw);self.assertEqual((backup/'bootstrap.json').read_bytes(),self.raw)
  self.assertTrue(preserve_bootstrap(self.old,backup,G)['already_present'])
  self.old.rename(self.new);backup.rename(self.old)
  self.assertEqual((self.old/'bootstrap.json').read_bytes(),self.raw)
  self.assertEqual((self.new/'bootstrap.json').read_bytes(),self.raw)
 def test_concurrent_same_configuration_publishes_once(self):
  with concurrent.futures.ThreadPoolExecutor(max_workers=4)as pool:
   results=list(pool.map(lambda _:preserve_bootstrap(self.old,self.new,G),range(8)))
  self.assertTrue(all(r['preserved']for r in results))
  self.assertEqual((self.new/'bootstrap.json').read_bytes(),self.raw)
  self.assertEqual(list(self.new.glob('*.pending')),[])
 def test_idempotent_same_configuration(self):
  preserve_bootstrap(self.old,self.new,G);self.assertTrue(preserve_bootstrap(self.old,self.new,G)['already_present'])
 def test_wrong_chain_never_copies(self):
  with self.assertRaises(ValueError):preserve_bootstrap(self.old,self.new,'8'*64)
  self.assertFalse((self.new/'bootstrap.json').exists())
 def test_existing_custom_configuration_never_overwritten(self):
  other=b'custom';(self.new/'bootstrap.json').write_bytes(other)
  with self.assertRaises(ValueError):preserve_bootstrap(self.old,self.new,G)
  self.assertEqual((self.new/'bootstrap.json').read_bytes(),other)
 def test_oversized_or_duplicate_configuration_refused(self):
  (self.old/'bootstrap.json').write_bytes(b' '*8193)
  with self.assertRaises(ValueError):preserve_bootstrap(self.old,self.new,G)
  (self.old/'bootstrap.json').write_text('{"schema":"x","schema":"y"}')
  with self.assertRaises(ValueError):preserve_bootstrap(self.old,self.new,G)
 def test_missing_optional_configuration_is_not_invented(self):
  (self.old/'bootstrap.json').unlink();self.assertFalse(preserve_bootstrap(self.old,self.new,G)['preserved'])
 def test_destination_outside_runtime_is_refused(self):
  outside=self.new/'child';outside.mkdir()
  with self.assertRaises(ValueError):preserve_bootstrap(self.old,outside,G)
if __name__=='__main__':unittest.main()
