from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import io,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import horizon_network_worker as module

class Monitor:
    def __init__(self,trace):self.trace=trace;self.identities=[]
    def snapshot(self):self.trace.append('snapshot');return dict(state='unknown',available=False)
    def select_identity(self,address):self.identities.append(address)
    def start(self):self.trace.append('monitor_start')
    def close(self):self.trace.append('monitor_close')

class BindingTests(unittest.TestCase):
    def worker(self):
        worker=object.__new__(module.NetworkWorker);worker.trace=[];worker.admission=Monitor(worker.trace)
        worker.args=SimpleNamespace(miner=None,wallet='fixture',wait_for_network_sync=False)
        worker.output_lock=threading.Lock();worker.running=threading.Event();worker.running.set();worker.quitting=threading.Event()
        worker.controls=lambda:None
        return worker
    def test_emit_uses_only_cached_snapshot(self):
        worker=self.worker();worker.rpc=SimpleNamespace(call=lambda *a,**kw:self.fail('emit performed RPC'))
        output=io.StringIO()
        with patch('sys.stdout',output):self.assertTrue(worker.emit(dict(schema='fixture')))
        self.assertEqual(json.loads(output.getvalue())['network_admission'],dict(state='unknown',available=False));self.assertEqual(worker.trace,['snapshot'])
    def test_output_loss_retains_original_checkpoint_pause_and_cancels_cpu_wait(self):
        class Broken(io.StringIO):
            def write(self,value):raise OSError('owned output fixture disconnected')
        worker=self.worker();worker.source=SimpleNamespace(cancel_cpu_transport=lambda reason:worker.trace.append('cpu_cancel'))
        with tempfile.TemporaryDirectory()as directory:
            worker.root=Path(directory)
            with patch('sys.stdout',Broken()):self.assertFalse(worker.emit(dict(schema='fixture')))
            self.assertFalse(worker.running.is_set());self.assertTrue(worker.quitting.is_set());self.assertTrue((worker.root/'pause.request').exists());self.assertIn('cpu_cancel',worker.trace)
    def test_selected_identity_is_observed_without_an_extra_wallet_choice(self):
        worker=self.worker();calls=[]
        def rpc(method,args,wallet=False):
            calls.append(method)
            if method=='getaddressesbylabel':return {'already-selected-fixture':{}}
            self.fail('diagnostic created another wallet identity')
        worker.rpc=SimpleNamespace(call=rpc)
        self.assertEqual(worker.address(),'already-selected-fixture');self.assertEqual(calls,['getaddressesbylabel']);self.assertEqual(worker.admission.identities,['already-selected-fixture'])
    def test_monitor_close_covers_verification_wallet_and_sync_refusals(self):
        for failure in('verify','loadwallet','sync'):
            with self.subTest(failure=failure):
                worker=self.worker();worker.args.wait_for_network_sync=True;worker.source=SimpleNamespace(close=lambda:worker.trace.append('source_close'))
                def verify():
                    worker.trace.append('verify')
                    if failure=='verify':raise RuntimeError('verification fixture refusal')
                def rpc(method,args=None):
                    worker.trace.append(method)
                    if method=='listwallets':return []
                    if method=='loadwallet'and failure=='loadwallet':raise RuntimeError('wallet fixture refusal')
                worker.rpc=SimpleNamespace(verify=verify,call=rpc)
                with patch.object(module,'wait_for_network',lambda w:False):
                    if failure=='sync':worker.run()
                    else:
                        with self.assertRaises(RuntimeError):worker.run()
                self.assertEqual(worker.trace[-1],'monitor_close')
                self.assertEqual(worker.trace.count('monitor_close'),1)
                if failure=='sync':self.assertIn('monitor_start',worker.trace);self.assertIn('source_close',worker.trace)
    def test_unknown_diagnosis_does_not_become_scheduler_authority(self):
        worker=self.worker();worker.rpc=SimpleNamespace(verify=lambda:None,call=lambda method,args=None:['fixture'])
        class Flow:
            closed=True
            def __init__(self,parent):self.parent=parent
            def run(self):self.parent.trace.append('normal_flow_run')
        with patch('horizon_mining_flow.MiningFlow',Flow):worker.run()
        self.assertEqual(worker.trace,['monitor_start','normal_flow_run','monitor_close'])

if __name__=='__main__':unittest.main()
