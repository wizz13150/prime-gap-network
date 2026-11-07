"""Actual finite Windows ownership/affinity check; no GPU or producer benchmark."""
from pathlib import Path
import ctypes as C,json,os,struct,sys,tempfile,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from pgn_owned import WindowsOwnedProcess,_job_limit_bytes,image_hash


class PreparationOwnedTests(unittest.TestCase):
    def test_existing_default_job_limits_remain_byte_for_byte_identical(self):
        self.assertEqual(_job_limit_bytes().raw,b'\0'*16+struct.pack('<I',0x2000)+b'\0'*124)

    def test_explicit_budget_rejects_invalid_or_unbounded_inputs(self):
        good=dict(logical_cpu=0,memory_mib=128)
        for value in [{},dict(good,logical_cpu=True),dict(good,logical_cpu=64),
                      dict(good,memory_mib=True),dict(good,memory_mib=0),
                      dict(good,memory_mib=4097),dict(good,other=1)]:
            with self.assertRaises(ValueError):_job_limit_bytes(value)

    @unittest.skipUnless(os.name=='nt','Actual Windows Job required')
    def test_real_child_is_suspended_bound_pinned_idle_and_drained_without_gpu_lease(self):
        class CpuLease:
            def __init__(self):self.records=[];self.released=False
            def bind(self,pid,birth,**data):self.records.append(dict(pid=pid,birth=birth,**data))
            def release(self):self.released=True
        k=C.WinDLL('kernel32',use_last_error=True)
        k.GetCurrentProcess.restype=C.c_void_p
        k.GetProcessAffinityMask.argtypes=[C.c_void_p,C.POINTER(C.c_size_t),C.POINTER(C.c_size_t)]
        process=C.c_size_t();system=C.c_size_t();self.assertTrue(k.GetProcessAffinityMask(k.GetCurrentProcess(),C.byref(process),C.byref(system)))
        allowed=[i for i in range(min(64,os.cpu_count()or 1))if process.value&(1<<i)]
        cpu=4 if 4 in allowed else allowed[0]
        code="import ctypes as c,json; k=c.WinDLL('kernel32'); k.GetCurrentProcess.restype=c.c_void_p; h=k.GetCurrentProcess(); k.GetPriorityClass.argtypes=[c.c_void_p]; k.GetProcessAffinityMask.argtypes=[c.c_void_p,c.POINTER(c.c_size_t),c.POINTER(c.c_size_t)]; a=c.c_size_t(); b=c.c_size_t(); assert k.GetProcessAffinityMask(h,c.byref(a),c.byref(b)); print(json.dumps(dict(affinity=a.value,priority=k.GetPriorityClass(h))))"
        lease=CpuLease();owned=None
        with tempfile.TemporaryDirectory()as temp:
            folder=Path(temp)
            with (folder/'stdout').open('wb')as out:
                try:
                    owned=WindowsOwnedProcess([sys.executable,'-B','-I','-c',code],dict(os.environ),lease,stdout=out,cwd=str(folder),cpu_budget=dict(logical_cpu=cpu,memory_mib=128))
                    self.assertFalse(owned.resumed)
                    identity=dict(pid=owned.pid,birth=owned.birth,image_sha256=image_hash(sys.executable),configuration_sha256='a'*64,wal_sequence=0)
                    path=folder/'identity.json';path.write_text(json.dumps(identity),encoding='utf-8')
                    owned.commit_identity(path);owned.resume();self.assertEqual(owned.wait(10),0)
                finally:
                    if owned:owned.close(force=True)
            observed=json.loads((folder/'stdout').read_bytes())
            self.assertEqual(observed,dict(affinity=1<<cpu,priority=0x40))
            self.assertTrue(lease.released);self.assertIsNone(owned.job);self.assertIsNone(owned._handle)
            self.assertEqual(lease.records[0]['state'],'SUSPENDED_ASSIGNED_JOB')


if __name__=='__main__':unittest.main()
