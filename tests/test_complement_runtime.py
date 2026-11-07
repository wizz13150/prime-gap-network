"""Explicit qualified backend routing, exact scope and recovery; no CUDA."""
from pathlib import Path
from types import SimpleNamespace
import importlib.util,json,sys,tempfile,unittest,os
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/engine/shared_composite'),str(ROOT/'src/protocol')]
import horizon_complement_runtime as backend
from horizon_complement_feed import prepare
from horizon_engine_host import EngineService
import pgn_plan
s=importlib.util.spec_from_file_location('feed_fixture',ROOT/'tests/test_complement_feed.py');f=importlib.util.module_from_spec(s);s.loader.exec_module(f)


class ComplementRuntimeTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        self.worker=self.root/'worker.exe';self.worker.write_bytes(b'fixture image, not a GPU execution')
        self.table=self.root/'orders.bin';self.table.write_bytes(b'fixture table')
        self.feed=prepare(f.fixture(),0,f.forbidden)
        self.meta=dict(image_sha256=backend.IMAGE_SHA,source_manifest_sha256=backend.SOURCE_SHA,
                       profile_k=64,build_arch=86,resident_job_schema='SCOUT_RESIDENT_JOB_V1')
        self.hashes={self.worker.resolve():backend.IMAGE_SHA,self.table.resolve():backend.TABLE_SHA}
        self.hashpatch=patch.object(backend,'file_sha',side_effect=lambda p:self.hashes[Path(p).resolve()]);self.hashpatch.start();self.addCleanup(self.hashpatch.stop)
        self.metapatch=patch.object(backend,'module_metadata',return_value=self.meta);self.metapatch.start();self.addCleanup(self.metapatch.stop)
        self.runtime=backend.ComplementRuntime(self.worker,self.table,self.feed,self.root/'cache',86)
        self.command=[str(self.worker),'--start',str(f.START),'--length','1000']

    def test_no_unqualified_image_profile_source_or_architecture(self):
        for key,value in [('image_sha256','0'*64),('source_manifest_sha256','0'*64),('profile_k',32),('profile_k',64.0),('build_arch',120),('build_arch',86.0),('resident_job_schema','other')]:
            old=self.meta[key];self.meta[key]=value
            try:
                with self.subTest(key=key),self.assertRaises(ValueError):backend.ComplementRuntime(self.worker,self.table,self.feed,self.root/'cache',86)
            finally:self.meta[key]=old
        with self.assertRaises(ValueError):backend.ComplementRuntime(self.worker,self.table,self.feed,self.root/'cache',120)
        self.hashes[self.table.resolve()]='0'*64
        with self.assertRaises(ValueError):backend.ComplementRuntime(self.worker,self.table,self.feed,self.root/'cache',86)

    def test_issued_domain_binds_configuration_without_mutating_it(self):
        cfg=dict(start=str(f.START),end=str(f.START+1000),ticket_id='a'*64)
        bound=self.runtime.bind_configuration(cfg)
        self.assertNotIn('complement_feed_sha256',cfg)
        self.assertEqual(bound['complement_feed_sha256'],self.feed.fingerprint)
        with self.assertRaises(ValueError):self.runtime.bind_configuration(dict(cfg,start=str(f.START-1)))
        self.assertEqual(self.runtime.workers(),[(64,self.worker.resolve())])

    def test_plan_never_allocates_factor_pages_or_changes_original_arguments(self):
        original=list(self.command)
        result=self.runtime.plan([*self.command,'--plan'])
        self.assertEqual(self.command,original)
        self.assertIn('--composite-small-factors',result)
        self.assertNotIn('--composite-page-directory',result)
        self.assertFalse((self.root/'cache').exists())

    def test_native_resume_binds_new_physical_pages_to_actual_progress(self):
        first=self.runtime.launch(self.command,0);resumed=self.runtime.launch(self.command,30)
        a=Path(first[first.index('--composite-page-directory')+1]);b=Path(resumed[resumed.index('--composite-page-directory')+1])
        self.assertNotEqual(a,b)
        self.assertEqual(json.loads((a/'manifest.json').read_bytes())['owned_start'],str(f.START))
        self.assertEqual(json.loads((b/'manifest.json').read_bytes())['owned_start'],str(f.START+30))
        self.assertEqual(self.runtime.launch(self.command,30),resumed)
        for p in [-1,True,1000]:
            with self.subTest(progress=p),self.assertRaises(ValueError):self.runtime.launch(self.command,p)

    def test_changed_image_and_preexisting_override_are_rejected(self):
        with self.assertRaises(ValueError):self.runtime.plan([*self.command,'--composite-small-factors'])
        self.hashes[self.worker.resolve()]='0'*64
        with self.assertRaises(ValueError):self.runtime.launch(self.command,0)

    def test_real_engine_service_routes_both_plan_and_launch_through_backend(self):
        observed=[];service=EngineService.__new__(EngineService);service.complement=self.runtime
        service.manager=SimpleNamespace(query=lambda command:observed.append(command)or {'controlled_plan':True})
        self.assertEqual(service.workers(),[(64,self.worker.resolve())])
        self.assertIn('--composite-small-factors',service.native_plan([*self.command,'--plan']))
        self.assertIn('--composite-page-directory',service.native_command(self.command,30))
        service.complement=None
        self.assertIs(service.native_command(self.command,0),self.command)

    def test_cold_and_resident_queries_receive_identical_complement_options(self):
        commands=[];raw={'fixture_plan':True}
        # Controlled native observations: this test checks routing, not a memory
        # admission claim; actual CUDA plan admission has its own receipt.
        with patch.object(pgn_plan,'module_metadata',return_value=self.meta), \
             patch.object(pgn_plan,'file_sha',return_value=backend.IMAGE_SHA), \
             patch.object(pgn_plan,'bind_plan',side_effect=lambda value,*_:value), \
             patch.object(pgn_plan.subprocess,'run',side_effect=lambda command,**_:commands.append(command)or SimpleNamespace(returncode=0,stdout=json.dumps(raw))):
            cold=pgn_plan.query_plan(self.worker,f.START,1000,{}, {},
                plan_provider=lambda _:None,command_transform=self.runtime.plan)
            resident=pgn_plan.query_plan(self.worker,f.START,1000,{}, {},
                plan_provider=lambda command:commands.append(command)or raw,command_transform=self.runtime.plan)
        self.assertEqual(cold,resident);self.assertEqual(commands[0],commands[1])
        self.assertIn('--composite-canonical-table',commands[0])

    def test_readonly_presentation_requires_exact_complement_identity_and_retention(self):
        from horizon_engine_capabilities import qualified_presentation_identity
        from pgn_native_contract import PROTOCOL
        cfg=dict(start=str(f.START),end=str(f.START+1000),preseal_required=True,
                 complement_feed_sha256=self.feed.fingerprint)
        identity=dict(worker_sha256=backend.IMAGE_SHA,worker_source_sha256=backend.SOURCE_SHA,
                      profile_k=64,native_protocol=PROTOCOL)
        # These artifacts are qualified for Windows only; Linux must refuse them.
        self.assertEqual(qualified_presentation_identity(cfg,identity),os.name=='nt')
        for key,value in [('worker_sha256','0'*64),('worker_source_sha256','0'*64),
                          ('profile_k',32),('profile_k',64.0),('native_protocol','other')]:
            self.assertFalse(qualified_presentation_identity(cfg,dict(identity,**{key:value})),key)
        for key,value in [('preseal_required',False),('complement_feed_sha256','unbound'),
                          ('start','not a position'),('end',str(1<<95))]:
            self.assertFalse(qualified_presentation_identity(dict(cfg,**{key:value}),identity),key)

    def test_legacy_presentation_identity_remains_compatible(self):
        from horizon_engine_capabilities import qualified_presentation_identity,IMAGES
        self.assertTrue(qualified_presentation_identity({},dict(worker_sha256=next(iter(IMAGES.values())))))

    def test_only_exact_qualified_pairs_are_admitted_by_the_shared_registry(self):
        self.assertTrue(backend.qualified_complement_pair(backend.IMAGE_SHA,backend.SOURCE_SHA))
        for image,source in [(backend.IMAGE_SHA,'0'*64),('0'*64,backend.SOURCE_SHA),
                             ([],backend.SOURCE_SHA),(backend.IMAGE_SHA,{}),
                             ('7D43189671045DFED0C1D5A2CE84061AC81E0A04C4C0AE4F379F4AF55DD46D75',backend.SOURCE_SHA)]:
            self.assertFalse(backend.qualified_complement_pair(image,source))
        with self.assertRaises(TypeError):backend.QUALIFIED_VARIANTS['0'*64]='0'*64

    def test_each_qualified_variant_routes_with_its_own_exact_image_pin(self):
        for image,source in backend.QUALIFIED_VARIANTS.items():
            with self.subTest(image=image):
                architecture=backend.qualified_complement_architecture(image,source)
                self.meta.update(image_sha256=image,source_manifest_sha256=source,build_arch=architecture)
                self.hashes[self.worker.resolve()]=image
                runtime=backend.ComplementRuntime(self.worker,self.table,self.feed,self.root/'cache',architecture)
                with self.assertRaises(ValueError):backend.qualified_inputs(self.worker,self.table,120 if architecture==86 else 86)
                self.assertIn('--composite-small-factors',runtime.plan(self.command))
                self.assertIn('--composite-page-directory',runtime.launch(self.command,30))
                self.hashes[self.worker.resolve()]='0'*64
                with self.assertRaises(ValueError):runtime.plan(self.command)

    def test_malformed_cached_image_identity_is_a_rejection_not_a_ui_exception(self):
        from horizon_engine_capabilities import qualified_presentation_identity
        for value in [[],{},None,1]:
            self.assertFalse(qualified_presentation_identity({},dict(worker_sha256=value)))


if __name__=='__main__':unittest.main()
