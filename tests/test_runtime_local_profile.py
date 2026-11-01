import os,pathlib,sys,tempfile,unittest,json
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_hardware_probe import parse_gpu_csv,compatible_architecture,probe
from horizon_local_runtime import prepare
from horizon_core_rpc import CoreRpcError
ID='GPU-11111111-2222-3333-4444-555555555555'
class Rpc:
 def __init__(self,missing=False,error=None,owned=True):self.calls=[];self.missing=missing;self.error=error;self.owned=owned
 def verify(self):self.calls.append(('verify',[],False));return{}
 def call(self,method,params=None,wallet=False):
  self.calls.append((method,params,wallet))
  if method=='listwallets':return['test-wallet']
  if method=='getaddressesbylabel':
   if self.error:raise self.error
   if self.missing:raise CoreRpcError(-11,'No addresses with label')
   return {'tpgn1qowned':{}}
  if method=='getnewaddress':return'tpgn1qnewowned'
  if method=='getaddressinfo':return dict(ismine=self.owned,iswatchonly=False)
  raise AssertionError('Unexpected RPC '+method)
class HardwareTests(unittest.TestCase):
 def test_minor_compatibility_without_cross_major_guess(self):
  for actual,wanted in [('8.6',86),('8.9',86),('8.0',None),('9.0',None),('12.0',120),('12.1',120),('invalid',None)]:self.assertEqual(compatible_architecture(actual),wanted)
 def test_csv_quoted_names_and_unknown_optional_metrics(self):
  text=ID+',"NVIDIA, example",8.6,580.0,8192,128,0,N/A,N/A,40,1500,5001\n'
  rows=parse_gpu_csv(text);self.assertEqual(len(rows),1);r=rows[0];self.assertEqual(r['name'],'NVIDIA, example');self.assertEqual(r['memory_total_bytes'],str(8192*1048576));self.assertIsNone(r['power_watts']);self.assertEqual(r['implementation_architecture'],86)
 def test_duplicate_and_malformed_inventory_refused(self):
  line=ID+',GPU,8.6,580,8192,0,0,0,0,0,0,0\n'
  with self.assertRaises(ValueError):parse_gpu_csv(line+line)
  with self.assertRaises(ValueError):parse_gpu_csv('not,the,right,fields')
class LocalProfileTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=pathlib.Path(self.temp.name);(self.root/'network').mkdir();(self.root/'network'/('horizon-engine-host.exe'if os.name=='nt'else'horizon-engine-host')).write_bytes(b'fixture only')
  self.connection=dict(schema='PGN_CORE_LOCAL_CONNECTION_V1',genesis='1'*64,rpc_port=48932,wallet='test-wallet',cookie_file=str(self.root/'.cookie'))
  (self.root/'network/bootstrap.json').write_text(json.dumps(dict(schema='PGN_TESTNET_BOOTSTRAP_V1',genesis='1'*64,checkpoint=dict(height=100,hash='2'*64))))
  self.hardware=dict(schema='PGN_LOCAL_HARDWARE_V1',available=True,devices=[dict(id=ID,name='NVIDIA example',compute_capability='8.6',implementation_architecture=86)])
 def tearDown(self):self.temp.cleanup()
 def build(self,rpc,hardware=None):return prepare(self.root,self.root/'state',self.connection,hardware or self.hardware,rpc=rpc,module_checker=lambda bundle,arch:None)
 def test_existing_owned_address_reused_no_allocations(self):
  rpc=Rpc();v=self.build(rpc);p=v['profiles'][0]
  self.assertEqual(p['gpu_uuid'],ID);self.assertTrue(p['local']);self.assertFalse(v['mining_started']);self.assertFalse(v['allocation_requested'])
  self.assertIn('tpgn1qowned',p['network_arguments']);self.assertIn('--observe',p['arguments'])
  self.assertFalse(any(call[0]in['getnewaddress','getpgnticketrequest','submitblock','createwallet','loadwallet']for call in rpc.calls))
 def test_only_missing_label_creates_wallet_identity(self):
  rpc=Rpc(missing=True);v=self.build(rpc);self.assertIn('tpgn1qnewowned',v['profiles'][0]['network_arguments']);self.assertEqual(sum(call[0]=='getnewaddress'for call in rpc.calls),1)
 def test_wallet_failure_is_not_treated_as_missing_label(self):
  rpc=Rpc(error=CoreRpcError(-18,'Wallet not loaded'))
  with self.assertRaises(CoreRpcError):self.build(rpc)
  self.assertFalse(any(call[0]=='getnewaddress'for call in rpc.calls))
 def test_foreign_or_watch_only_address_not_used(self):
  with self.assertRaises(ValueError):self.build(Rpc(owned=False))
 def test_unknown_architecture_not_replaced_by_other_engine(self):
  hardware=dict(self.hardware,devices=[dict(self.hardware['devices'][0],implementation_architecture=None)])
  rpc=Rpc();v=self.build(rpc,hardware);self.assertEqual(v['profiles'],[]);self.assertEqual(len(v['unsupported']),1);self.assertFalse(any(call[0]=='getaddressesbylabel'and call[1][0].startswith('Horizon mining')for call in rpc.calls));self.assertEqual(v['audit_identity']['address'],'tpgn1qowned')
 def test_multiple_devices_have_separate_wallet_labels(self):
  second=dict(self.hardware['devices'][0],id='GPU-aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee')
  rpc=Rpc();v=self.build(rpc,dict(self.hardware,devices=[self.hardware['devices'][0],second]));self.assertEqual(len(v['profiles']),2)
  labels=[call[1][0]for call in rpc.calls if call[0]=='getaddressesbylabel'];self.assertEqual(len(set(labels)),2)
 def test_invalid_context_or_duplicate_device_refused(self):
  bad=dict(self.connection,schema='other')
  with self.assertRaises(ValueError):prepare(self.root,self.root/'state',bad,self.hardware,rpc=Rpc(),module_checker=lambda *args:None)
  with self.assertRaises(ValueError):self.build(Rpc(),dict(self.hardware,devices=self.hardware['devices']*2))
if __name__=='__main__':unittest.main()
