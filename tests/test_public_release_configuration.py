from pathlib import Path
import importlib.util,tempfile,unittest,zipfile
ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('public_release_check',ROOT/'tools/check_public_release.py');check=importlib.util.module_from_spec(spec);spec.loader.exec_module(check)
class PublicReleaseTests(unittest.TestCase):
 def check(self,entries):
  with tempfile.TemporaryDirectory()as directory:
   p=Path(directory)/'release.zip'
   with zipfile.ZipFile(p,'w',zipfile.ZIP_DEFLATED)as z:
    for name,value in entries.items():z.writestr(name,value)
   return check.inspect(p)
 def test_public_code_reference_and_common_bootstrap_are_allowed(self):
  r=self.check({'PGN/PGN-Horizon.exe':b'closed fixture','PGN/data/reference/prime-gap-records.json':'{}',
   'PGN/EngineRuntime/network/bootstrap.json':'{"tls":{"host":"testnet.example.org","port":8443}}'})
  self.assertTrue(r['PASS'])
 def test_personal_runtime_profiles_and_wallets_are_rejected(self):
  for name in('PGN/data/testnet/ui-runtime/worker-stream.json','PGN/wallets/example/wallet.dat','PGN/EngineRuntime/cpu-controllers.json'):
   self.assertFalse(self.check({name:'{}'})['PASS'],name)
 def test_personal_paths_and_embedded_credentials_are_rejected(self):
  for text in('{"program":"C:/Users/alice/private/miner.exe"}','rpcpassword=actual-password','-----BEGIN PRIVATE KEY-----\nfixture\n'):
   self.assertFalse(self.check({'PGN/defaults.conf':text})['PASS'])
 def test_zip_paths_cannot_escape_or_alias_a_public_component(self):
  for name in('../wallet.dat','C:/wallet.dat','PGN\\wallet.dat'):
   self.assertFalse(self.check({name:'x'})['PASS'])
  self.assertFalse(self.check({'PGN/a.json':'{}','pgn/A.json':'{}'})['PASS'])
if __name__=='__main__':unittest.main()
