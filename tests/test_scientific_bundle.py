"""Scientific bundle admission/wiring only; native image execution is separate."""
from pathlib import Path
from types import SimpleNamespace
import copy
import json
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ['src/engine/runtime_host','src/engine/shared_composite','src/protocol']]
import horizon_scientific_bundle as bundle
from horizon_complement_runtime import IMAGE_SHA


class ScientificBundleTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        (self.root/'bin').mkdir();(self.root/'network').mkdir()
        self.worker=self.root/'bin/prime-gap-worker-sm86-k64.exe';self.worker.write_bytes(b'controlled image fixture')
        self.table=self.root/'bin/canonical-orders.bin';self.table.write_bytes(b'controlled table fixture')
        self.producer=self.root/'bin/pgn-psp-tail-producer.exe';self.producer.write_bytes(b'controlled producer fixture')
        self.doc=dict(schema='PGN_SCOPED_SCIENTIFIC_SOURCE_V1',architecture=86,domain_end_exclusive='1000000000000000000000',
            worker=dict(path='bin/prime-gap-worker-sm86-k64.exe',sha256=IMAGE_SHA),
            table=dict(path='bin/canonical-orders.bin',sha256=bundle.TABLE_SHA),
            producer=dict(path='bin/pgn-psp-tail-producer.exe',sha256=bundle.PRODUCER_SHA),
            preparation=dict(threads=1,priority='idle',memory_mib=768,timeout_seconds=3600))
        self.qualified=patch.object(bundle,'qualified_inputs',return_value=(self.worker,self.table,dict(image_sha256=IMAGE_SHA)))
        self.qualified.start();self.addCleanup(self.qualified.stop)
        self.hash=patch.object(bundle,'file_sha',return_value=bundle.PRODUCER_SHA);self.hash.start();self.addCleanup(self.hash.stop)
        self.write(self.doc)
    def write(self,value):
        (self.root/bundle.DESCRIPTOR).write_text(json.dumps(value),encoding='utf-8')
    def test_exact_descriptor_returns_pinned_inputs_without_starting_runtime(self):
        result=bundle.load(self.root,86,required=True)
        self.assertEqual(result.worker,self.worker);self.assertEqual(result.producer,self.producer)
        self.assertEqual(result.memory_mib,768);self.assertEqual(result.timeout,3600)
        self.assertEqual(bundle.qualified_bundle_workers(self.root,86),[(64,self.worker)])
        self.assertEqual(set(p.name for p in self.root.iterdir()),{'bin','network'})
    def test_missing_required_descriptor_and_unsupported_architecture_never_fall_back(self):
        with self.assertRaises(bundle.UnsupportedScientificArchitecture):bundle.load(self.root,120)
        (self.root/bundle.DESCRIPTOR).unlink()
        self.assertIsNone(bundle.load(self.root,86))
        with self.assertRaises(ValueError):bundle.load(self.root,86,required=True)
        with patch('horizon_engine_capabilities.qualified_workers',return_value=['legacy'])as old:
            self.assertEqual(bundle.qualified_bundle_workers(self.root,86),['legacy']);self.assertEqual(old.call_count,1)
    def test_changed_paths_images_and_unbounded_resources_are_rejected(self):
        cases=[]
        for key,value in [('worker',dict(path='../worker.exe',sha256=IMAGE_SHA)),
                          ('producer',dict(path='bin/pgn-psp-tail-producer.exe',sha256='f'*64)),
                          ('domain_end_exclusive','1000000000000000000001'),('architecture',86.0)]:
            cases.append(dict(self.doc,**{key:value}))
        for key,value in [('threads',2),('threads',True),('priority','normal'),('memory_mib',4096),('timeout_seconds',0)]:
            doc=copy.deepcopy(self.doc);doc['preparation'][key]=value;cases.append(doc)
        with patch('horizon_engine_capabilities.qualified_workers',side_effect=AssertionError('Invalid scientific bundle fell back')):
            for doc in cases:
                self.write(doc)
                with self.subTest(doc=doc),self.assertRaises(ValueError):bundle.qualified_bundle_workers(self.root,86)
    def test_changed_actual_producer_digest_is_rejected(self):
        with patch.object(bundle,'file_sha',return_value='f'*64):
            with self.assertRaises(ValueError):bundle.load(self.root,86)
    def test_architecture_map_selects_its_exact_worker_without_a_fallback(self):
        doc=copy.deepcopy(self.doc);worker=doc.pop('worker');doc.pop('architecture')
        doc.update(schema='PGN_SCOPED_SCIENTIFIC_SOURCE_V2',workers={'86':worker,
            '120':dict(path='bin/prime-gap-worker-sm120-k64.exe',sha256='A'*64)})
        other=self.root/'bin/prime-gap-worker-sm120-k64.exe';other.write_bytes(b'separate architecture fixture')
        self.write(doc)
        with patch.object(bundle,'qualified_inputs',return_value=(other,self.table,dict(image_sha256='A'*64)))as inspect:
            result=bundle.load(self.root,120,required=True)
            self.assertEqual(result.worker,other);self.assertEqual(result.architecture,120)
            self.assertEqual(inspect.call_args.args,(other,self.table,120))
        self.assertEqual(bundle.load(self.root,86).worker,self.worker)
        with self.assertRaises(bundle.UnsupportedScientificArchitecture):bundle.load(self.root,121)
        doc['workers'].pop('120');self.write(doc)
        with self.assertRaises(bundle.UnsupportedScientificArchitecture):bundle.load(self.root,120)
    def test_architecture_map_rejects_cross_architecture_paths_and_extra_variants(self):
        doc=copy.deepcopy(self.doc);worker=doc.pop('worker');doc.pop('architecture')
        doc.update(schema='PGN_SCOPED_SCIENTIFIC_SOURCE_V2',workers={'120':worker})
        self.write(doc)
        with self.assertRaises(ValueError):bundle.load(self.root,120)
        doc['workers']={'86':worker,'121':worker};self.write(doc)
        with self.assertRaises(ValueError):bundle.load(self.root,86)
    def test_factory_wires_one_owned_resolver_and_shutdown_into_explicit_complement(self):
        stopping=threading.Event()
        with patch('pgn_prime_proof.PrimeProof')as proof,patch('horizon_preparation_provider.PreparationProvider')as provider,\
             patch('horizon_preparation_resolver.PreparationResolver')as resolver,\
             patch('horizon_complement_runtime.DeferredComplementRuntime')as runtime,\
             patch('horizon_engine_host.EngineService')as service,patch.object(bundle,'_processor',return_value=4):
            actual=bundle.create_engine(self.root,self.root/'state','GPU-fixture',86,lambda _:None,required=True,stopping=stopping)
            self.assertIs(actual,service.return_value)
            self.assertIs(service.call_args.kwargs['complement'],runtime.return_value)
            self.assertIs(resolver.call_args.kwargs['stopping'],stopping)
            self.assertEqual(resolver.call_args.kwargs['logical_cpu'],4)
            self.assertEqual(resolver.call_args.kwargs['memory_mib'],768)
            self.assertEqual(resolver.call_args.kwargs['timeout'],3600)
            self.assertIs(resolver.call_args.kwargs['prefetch'],True)
            self.assertEqual(provider.call_args.args[1],self.producer)


if __name__=='__main__':unittest.main()
