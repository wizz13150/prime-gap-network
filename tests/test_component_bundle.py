"""Publisher payload filtering; inert code only, strict client rules retained."""
from pathlib import Path
import hashlib,json,sys,tempfile,unittest,zipfile
P=Path(__file__).resolve().parents[1];sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import build_component_bundle as builder
import generate_component_manifest as component
import stage_component_bundle as stage
from test_component_staging import pe

class ComponentBundleTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.root=Path(self.temp.name);self.code=self.root/'code';self.code.mkdir();(self.code/'PGN-Horizon.exe').write_bytes(pe())
  self.marker='UpdateRuntime/_internal/cryptography-50.0.2.dist-info/REQUESTED';f=self.code/self.marker;f.parent.mkdir(parents=True);f.write_bytes(b'')
  self.context=self.root/'context.json';self.context.write_text(json.dumps(dict(schema='PGN_COMPONENT_STAGE_CONTEXT_V1',network='testnet',genesis_hash='5'*64,platform='windows-x86_64',compatibility_set='qa.cohort',minimum_sequence=1,installed=[dict(id='ui',version='1.0',abi='qa.1')],protected_roots=[str(self.code)])))
 def tearDown(self):self.temp.cleanup()
 def manifest(self,files):
  value=component.build(dict(release_version='1.0',network='testnet',published=False,components=[dict(id='ui',version='1.0',root=str(self.code),files=files)]));(self.code/'components.json').write_text(json.dumps(value))
 def test_only_known_empty_installer_marker_is_omitted(self):
  self.manifest(['PGN-Horizon.exe',self.marker]);r=builder.build(self.code,self.context,self.root/'bundle.zip',2,'1.0','qa.1')
  self.assertEqual(r['files'],1);self.assertEqual(r['omitted_optional_installer_metadata'],[self.marker]);self.assertTrue(r['privacy']['PASS'])
  with zipfile.ZipFile(self.root/'bundle.zip')as z:self.assertEqual(set(z.namelist()),{'component-bundle.json','payload/ui/PGN-Horizon.exe'})
 def test_other_empty_code_is_still_refused(self):
  (self.code/'empty.dll').write_bytes(b'');self.manifest(['PGN-Horizon.exe',self.marker,'empty.dll'])
  with self.assertRaisesRegex(ValueError,'Invalid file declaration'):builder.build(self.code,self.context,self.root/'bundle.zip',2,'1.0','qa.1')
  self.assertFalse((self.root/'bundle.zip').exists())
 def deployment(self):
  helper=self.code/'UpdateRuntime/horizon-updater.exe';helper.write_bytes(pe())
  value=dict(schema='PGN_UPDATE_DEPLOYMENT_V1',network='testnet',genesis_hash='5'*64,
   compatibility_set='qa.cohort',abi='qa.1',helper='UpdateRuntime/horizon-updater.exe',
   sha256=hashlib.sha256(helper.read_bytes()).hexdigest(),owned_update_protocol=1)
  return helper,value
 def test_missing_deployment_is_rejected_before_publication(self):
  helper,_=self.deployment();self.manifest(['PGN-Horizon.exe',self.marker,'UpdateRuntime/horizon-updater.exe'])
  with self.assertRaisesRegex(ValueError,'without its deployment.json'):
   builder.build(self.code,self.context,self.root/'bundle.zip',2,'1.0','qa.1')
  self.assertFalse((self.root/'bundle.zip').exists())
 def test_deployment_survives_actual_archive(self):
  _,value=self.deployment();(self.code/'UpdateRuntime/deployment.json').write_text(json.dumps(value))
  self.manifest(['PGN-Horizon.exe',self.marker,'UpdateRuntime/horizon-updater.exe','UpdateRuntime/deployment.json'])
  builder.build(self.code,self.context,self.root/'bundle.zip',2,'1.0','qa.1')
  with zipfile.ZipFile(self.root/'bundle.zip')as z:
   self.assertEqual(json.loads(z.read('payload/ui/UpdateRuntime/deployment.json')),value)
 def test_deployment_must_bind_actual_helper_and_network(self):
  helper,value=self.deployment();context=json.loads(self.context.read_bytes())
  for field,replacement in [('sha256','0'*64),('genesis_hash','6'*64),('abi','other.1'),('network','main')]:
   changed=dict(value,**{field:replacement});(self.code/'UpdateRuntime/deployment.json').write_text(json.dumps(changed))
   with self.assertRaises(ValueError):builder.validate_update_deployment(self.code,{'UpdateRuntime/horizon-updater.exe','UpdateRuntime/deployment.json'},context,'qa.1')
 def test_all_four_qualified_profile_sidecars_are_code_resources(self):
  for arch in (86,120):
   for profile in (8,16,32,64):
    stage._code_path(f'payload/gpu/EngineRuntime/bin/prime-gap-worker-sm{arch}-k{profile}.exe.module.json','gpu')
  for name in ('prime-gap-worker-sm86-k128.exe.module.json','prime-gap-worker-sm99-k64.exe.module.json','private.json'):
   with self.assertRaises(stage.StageError):stage._code_path('payload/gpu/EngineRuntime/bin/'+name,'gpu')
 def test_managed_network_host_requires_its_matching_bootstrap(self):
  context=json.loads(self.context.read_bytes());host=builder.PRIMARY['gpu'];paths={host}
  with self.assertRaisesRegex(ValueError,'missing bootstrap.json'):builder.validate_network_bootstrap(self.code,paths,context)
  name='EngineRuntime/network/bootstrap.json';file=self.code/name;file.parent.mkdir(parents=True);paths.add(name)
  file.write_text(json.dumps(dict(schema='PGN_TESTNET_BOOTSTRAP_V1',genesis='6'*64)))
  with self.assertRaisesRegex(ValueError,'different network'):builder.validate_network_bootstrap(self.code,paths,context)
  file.write_text(json.dumps(dict(schema='PGN_TESTNET_BOOTSTRAP_V1',genesis=context['genesis_hash'])))
  builder.validate_network_bootstrap(self.code,paths,context)
if __name__=='__main__':unittest.main()
