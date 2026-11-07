import io,json,pathlib,sys,unittest,zipfile
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_diagnostics import export,MAX_INPUT
class DiagnosticTests(unittest.TestCase):
 def capture(self,samples):return json.dumps(dict(schema='PGN_DIAGNOSTIC_CAPTURE_V1',samples=samples)).encode()
 def test_archive_is_bounded_status_only_and_redacts_user_paths(self):
  raw=export(self.capture([dict(phase='Searching',positions='1000',message='C:/Users/example/Application/status.json')]))
  with zipfile.ZipFile(io.BytesIO(raw))as archive:
   self.assertEqual(set(archive.namelist()),{'diagnostics.json','README.txt'});body=archive.read('diagnostics.json');self.assertNotIn(b'example',body);self.assertIn(b'<user>',body);self.assertLess(sum(i.file_size for i in archive.infolist()),MAX_INPUT)
 def test_private_values_and_audit_answers_refused_recursively(self):
  for key in('private_key','cookie_file','signature','commitment_nonce','counts','expected_counts','audit_answers','report_root','scopes','best','wif'):
   with self.subTest(key=key),self.assertRaises(ValueError):export(self.capture([dict(nested={key:'DO NOT EXPORT'})]))
 def test_inline_credentials_are_redacted(self):
  with zipfile.ZipFile(io.BytesIO(export(self.capture([dict(message='Authorization: Basic secret-value')]))))as archive:self.assertNotIn(b'secret-value',archive.read('diagnostics.json'))
 def test_capacity_empty_depth_and_nonfinite_refused(self):
  for raw in(b'x'*(MAX_INPUT+1),self.capture([]),self.capture([{}]*1801),self.capture([dict(rate=float('nan'))])):
   with self.assertRaises((ValueError,TypeError)):export(raw)
  value={}
  for _ in range(14):value=dict(next=value)
  with self.assertRaises(ValueError):export(self.capture([value]))
 def test_native_prv_in_free_text_is_refused_without_a_private_field_name(self):
  for message in ('prv'+'a'*64,'Cannot import descriptor wpkh(prv'+'b'*64+')#checksum','prefix PRV'+'C'*64+' suffix'):
   with self.subTest(kind='native private key'),self.assertRaises(ValueError):export(self.capture([dict(message=message)]))
 def test_credential_and_seed_fields_are_refused(self):
  for key in ('seed','encrypted_seed','owner_token','access_token','credential','api_key','apikey'):
   with self.subTest(key=key),self.assertRaises(ValueError):export(self.capture([dict(nested={key:'synthetic-sensitive-value'})]))
 def test_public_hashes_are_not_mistaken_for_native_private_keys(self):
  with zipfile.ZipFile(io.BytesIO(export(self.capture([dict(block_hash='a'*64,phase='Searching')]))))as archive:
   self.assertIn(('a'*64).encode(),archive.read('diagnostics.json'))
if __name__=='__main__':unittest.main()
