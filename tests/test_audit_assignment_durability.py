"""Confirmed assignment survives a real BUSY before seal lookup; no node/GPU."""
from contextlib import contextmanager
from pathlib import Path
import importlib.util,json,sqlite3,sys,threading,time,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_audit_answers import AuditAnswers

class AssignmentDurabilityTests(unittest.TestCase):
    def test_busy_after_confirmed_assignment_resumes_same_audit(self):
        original=AuditAnswers.database
        injected=[];errors=[];holders=[]
        @contextmanager
        def contended(catalogue):
            if (not injected and threading.current_thread().name=='audit-preparation-0'):
                injected.append(True)
                with original(catalogue):pass
                held=threading.Event()
                def writer():
                    db=sqlite3.connect(catalogue.file,timeout=1)
                    try:
                        db.execute('BEGIN EXCLUSIVE');held.set();time.sleep(1.5)
                    finally:db.rollback();db.close()
                thread=threading.Thread(target=writer);holders.append(thread);thread.start()
                if not held.wait(2):raise AssertionError('writer did not acquire fixture lock')
            try:
                with original(catalogue)as db:yield db
            except sqlite3.OperationalError as error:
                # The first contention must leave the confirmed assignment
                # recoverable before the generic retry loop sees the error.
                journal=catalogue.folder/'current.json'
                errors.append((getattr(error,'sqlite_errorcode',None),journal.is_file()))
                raise
        spec=importlib.util.spec_from_file_location('durability_cycle',ROOT/'tests/test_mining_flow_cycle.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        try:
            with patch.object(AuditAnswers,'database',contended):
                case=module.MiningFlowCycleTests('test_lab_actors_keep_legacy_computation_before_native_service')
                case.debug()
        finally:
            for thread in holders:thread.join(2)
        self.assertEqual(injected,[True]);self.assertTrue(errors,'real SQLite BUSY boundary was not reached')
        self.assertEqual(errors[0],(sqlite3.SQLITE_BUSY,True))

if __name__=='__main__':unittest.main()
