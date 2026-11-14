"""Core refusal preserves durable work; retry uses the original audit seal."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import errno,importlib.util,json,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_network_worker import NetworkWorker
from horizon_mining_flow import MiningFlow

class CoreOutageTests(unittest.TestCase):
    def test_repeated_cold_core_refusal_never_replaces_saved_ticket_or_result(self):
        with tempfile.TemporaryDirectory()as temporary:
            root=Path(temporary);ticket=root/'tickets'/('a'*64);ticket.mkdir(parents=True)
            files={root/'controller.json':b'{"stage":"ASSIGNED","preserve":"owned ticket"}\n',
                   ticket/'network-report.json':b'{"nonce":"original durable result"}\n'}
            for file,raw in files.items():file.write_bytes(raw)
            args=SimpleNamespace(state_root=root,rpc_port=1,cookie=root/'fixture-cookie',genesis='b'*64,wallet='private-test-only')
            for _ in range(3):
                with patch('horizon_network_worker.CoreRpc',side_effect=ConnectionRefusedError(errno.ECONNREFUSED,'fixture Core offline')):
                    with self.assertRaises(ConnectionRefusedError):NetworkWorker(args)
                for file,raw in files.items():self.assertEqual(file.read_bytes(),raw)

    def test_core_returns_and_same_sealed_audit_retries_without_OFF_ON_or_nonce(self):
        spec=importlib.util.spec_from_file_location('outage_seal_fixture',ROOT/'tests/test_audit_seal_recovery.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        fixture=module.SealRecoveryTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        original=fixture.audits.apply;attempts=[]
        def apply(*args,**kwargs):
            attempts.append(args)
            if len(attempts)==1:raise ConnectionRefusedError(errno.ECONNREFUSED,'fixture Core offline')
            return original(*args,**kwargs)
        fixture.audits.apply=apply
        worker=fixture.audits.worker;worker.quitting=threading.Event()
        flow=SimpleNamespace(worker=worker,pending_audit=lambda _:None)
        process=fixture.audits.process_one
        def one():
            result=process()
            if result=='committed':worker.quitting.set()
            return result
        fixture.audits.process_one=one
        with patch('horizon_network_audits.secrets.token_hex',side_effect=AssertionError('retry regenerated original nonce')):
            thread=threading.Thread(target=MiningFlow.audit_work,args=(flow,fixture.audits,0));thread.start()
            try:
                thread.join(5);self.assertFalse(thread.is_alive());self.assertTrue(worker.running.is_set())
            finally:worker.quitting.set();thread.join(2)
        self.assertEqual(len(attempts),2);self.assertEqual(attempts[0],attempts[1])
        self.assertEqual(fixture.compute_calls,0);self.assertEqual(len(fixture.commits),1)
        self.assertEqual(json.loads(fixture.named.read_bytes())['answer'],fixture.answer)

if __name__=='__main__':unittest.main()
