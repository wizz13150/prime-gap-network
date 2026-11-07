"""Windows producer CLI: independent exhaustive small-domain oracle and refusal."""
from pathlib import Path
import hashlib,json,os,struct,subprocess,sys,tempfile,time,unittest
EXE=Path(sys.argv.pop(1)).resolve()if len(sys.argv)>1 else None
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from horizon_residual_bundle import bind


def prime(n):
    return n>=2 and all(n%d for d in range(2,int(n**.5)+1))


class ScopedProducerTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.base=Path(self.tmp.name)

    def run_cli(self,*args,exit=0):
        command=[str(EXE),*map(str,args)]
        p=subprocess.run(command,capture_output=True,timeout=15,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(p.returncode,exit,p.stderr.decode(errors='replace')[-1000:])
        # Controlled receipt-shape fixture, not an assertion of per-call Job
        # ownership. The integration driver separately supplies a real Job receipt.
        self.execution=dict(args=command,exit=p.returncode,error=None,all_owned_jobs_drained=True,GPU_used=False)
        return p

    def batch(self,scopes):
        folder=self.base/'fresh';self.run_cli(20,folder,*[v for pair in scopes for v in pair])
        receipt=json.loads((folder/'COMPLETE.json').read_bytes())
        self.assertTrue(receipt['complete_factor_scan']);self.assertFalse(receipt['GPU_used'])
        self.assertEqual(receipt['factor_begin'],21);self.assertEqual(receipt['factor_end_exclusive'],int((scopes[-1][1]-1)**.5)+1)
        self.assertEqual(receipt['wire_format'],'offset_factor_cofactor_u64le')
        self.assertFalse(receipt['production_admitted']or receipt['full95_coverage']or receipt['prime_ended_coverage_proved'])
        found=set()
        for record,(a,b)in zip(receipt['scopes'],scopes):
            self.assertEqual((int(record['begin']),int(record['end_exclusive'])),(a,b))
            raw=(folder/record['file']).read_bytes();self.assertEqual(len(raw),24*record['witnesses'])
            for offset,p,q in struct.iter_unpack('<QQQ',raw):
                n=a+offset;self.assertTrue(a<=n<b);self.assertEqual(p*q,n);self.assertGreater(p,20);self.assertGreaterEqual(q,p);self.assertTrue(prime(p));found.add(n)
        small=[p for p in range(2,21)if prime(p)]
        expected={n for a,b in scopes for n in range(a,b)if n%2 and not prime(n)and all(n%p for p in small)and pow(2,n-1,n)==1}
        observed={n for n in found if pow(2,n-1,n)==1}
        self.assertEqual(observed,expected)
        self.assertIn(2047,observed)
        return folder,receipt

    def test_complete_3_to_8000_against_independent_fermat_oracle(self):
        self.batch([(3,8000)])

    def test_disjoint_scopes_preserve_exact_ownership_and_no_credit_for_holes(self):
        _,r=self.batch([(3,3000),(3500,8000)])
        self.assertEqual(r['positions'],'7497')

    def test_fresh_directory_only_preserves_existing_results(self):
        folder,_=self.batch([(3,8000)])
        before={p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in folder.iterdir()}
        self.run_cli(20,folder,3,8000,exit=1)
        self.assertEqual(before,{p.name:hashlib.sha256(p.read_bytes()).hexdigest()for p in folder.iterdir()})

    def test_invalid_input_never_creates_output_or_complete_receipt(self):
        for i,args in enumerate([(1,3,8),(10000001,3,8000),(20,3,8001),(20,8000,3),
                                 (20,'03',8000),(20,-3,8000),(20,3,4000,3999,8000),(20,3500,8000,3,3000)]):
            path=self.base/str(i)
            with self.subTest(args=args):self.run_cli(args[0],path,*args[1:],exit=1);self.assertFalse(path.exists())

    def test_interruption_leaves_no_complete_receipt(self):
        folder=self.base/'interrupted'
        child=subprocess.Popen([str(EXE),'10000000',str(folder),'150000000000000000000','150000001000000000000'],
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        try:
            end=time.monotonic()+3
            while not folder.is_dir()and time.monotonic()<end:
                self.assertIsNone(child.poll());time.sleep(.01)
            self.assertTrue(folder.is_dir());self.assertIsNone(child.poll())
        finally:
            if child.poll()is None:child.terminate()
            child.wait(timeout=5)
        self.assertFalse((folder/'COMPLETE.json').exists())

    def test_describe_neither_launches_gpu_nor_claims95bit_completeness(self):
        r=json.loads(self.run_cli('--describe').stdout)
        self.assertFalse(r['GPU']or r['claims_full95_coverage']or r['claims_prime_end_sentinels'])
        self.assertEqual(r['maximum_scopes'],64)

    def test_unicode_output_path_preserves_exact_witness_bytes(self):
        plain,_=self.batch([(3,8000)])
        for name in ['préparation-é','準備_日本']:
            folder=self.base/name
            self.run_cli(20,folder,3,8000)
            self.assertEqual((folder/'scope-0.factors').read_bytes(),(plain/'scope-0.factors').read_bytes())
            self.assertTrue(json.loads((folder/'COMPLETE.json').read_bytes())['complete_factor_scan'])

    def test_bound_outputs_require_exact_image_arguments_and_successful_receipt(self):
        folder,_=self.batch([(3,8000)]);h=hashlib.sha256(EXE.read_bytes()).hexdigest()
        receipt=dict(self.execution)
        result=bind(folder,20,[(3,8000)],h,receipt)
        self.assertTrue(result.complete_factor_scan)
        self.assertFalse(result.whole95_coverage or result.proves_primary_factor_coverage)
        self.assertIn(2047,{n for n,p,q in result.witnesses[0]})
        for field,value in [('exit',1),('all_owned_jobs_drained',False),('GPU_used',True),('error','Interrupted')]:
            bad=dict(receipt,**{field:value})
            with self.subTest(field=field),self.assertRaises(ValueError):bind(folder,20,[(3,8000)],h,bad)
        with self.assertRaises(ValueError):bind(folder,20,[(3,8000)],'0'*64,receipt)
        with self.assertRaises(ValueError):bind(folder,20,[(3,7999)],h,receipt)

    def test_bound_outputs_reject_corruption_and_partial_metadata(self):
        folder,_=self.batch([(3,8000)]);h=hashlib.sha256(EXE.read_bytes()).hexdigest();execution=dict(self.execution)
        file=folder/'scope-0.factors';raw=file.read_bytes();self.assertTrue(raw)
        file.write_bytes(raw[:-1])
        with self.assertRaises(ValueError):bind(folder,20,[(3,8000)],h,execution)
        bad=bytearray(raw);bad[8]^=1;file.write_bytes(bad)
        with self.assertRaises(ValueError):bind(folder,20,[(3,8000)],h,execution)
        file.write_bytes(raw);meta=json.loads((folder/'COMPLETE.json').read_bytes());meta['complete_factor_scan']=False
        (folder/'COMPLETE.json').write_text(json.dumps(meta))
        with self.assertRaises(ValueError):bind(folder,20,[(3,8000)],h,execution)


if __name__=='__main__':
    if EXE is None or not EXE.is_file():raise SystemExit('Pass the qualified producer executable path')
    unittest.main()
