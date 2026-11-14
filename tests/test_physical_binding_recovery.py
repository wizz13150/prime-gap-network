from pathlib import Path
from types import SimpleNamespace
import hashlib
import json
import sys
import tempfile
import threading
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/protocol'),str(ROOT/'src/engine/runtime_host')]
from horizon_cycle_execution import CycleExecution
from horizon_mining_flow import MiningFlow
from report_commitment import canonical_bytes

class PhysicalBindingRecoveryTests(unittest.TestCase):
    def fixture(self,folder):
        journal=CycleExecution(folder,'a'*64);cycle='b'*64;journal.prepare(cycle,{cycle})
        audit=dict(request_id='c'*64,slot=1,attempt=0,ranges=[dict(start='100',end='200')])
        journal.bind(cycle,0,audit)
        def native(*args,**kwargs):raise AssertionError('Durable physical binding fell through to Native authorization receipts')
        flow=object.__new__(MiningFlow);flow.physical_audits=journal
        flow.unbound_service_lock=threading.RLock()
        flow.unbound_service_audits=set();flow.unbound_service_completed=set()
        flow.prefetch=SimpleNamespace(native_service_status=None)
        flow.cycles=SimpleNamespace(value={'cycles':[dict(stage='prepared',hold='',work={'request_id':cycle})]},
            extra_audit_cycle=lambda ident:None,bound_audit=native,pending=native)
        flow.audit_lanes=[SimpleNamespace(folder=Path(folder)),SimpleNamespace(folder=Path(folder))]
        flow.cycle_audit_positions={};flow.gpu=SimpleNamespace(wake=lambda:None)
        return flow,journal,cycle,audit

    def test_restart_recovers_binding_before_reserving_an_extra(self):
        with tempfile.TemporaryDirectory() as folder:
            flow,journal,cycle,audit=self.fixture(folder)
            flow.bind_audit_cycle(0,audit)
            self.assertEqual(audit['physical_cycle'],cycle)
            self.assertEqual(json.loads((Path(folder)/'current.json').read_bytes())['physical_cycle'],cycle)
            self.assertEqual(journal.snapshot(cycle)[0]['stage'],'assigned')

    def test_confirmed_callback_recovers_binding_without_transient_field(self):
        with tempfile.TemporaryDirectory() as folder:
            flow,journal,cycle,audit=self.fixture(folder)
            flow.audit_committed(0,audit,'d'*64)
            self.assertEqual(journal.snapshot(cycle)[0]['stage'],'committed')
            self.assertEqual(journal.snapshot(cycle)[0]['block'],'d'*64)
            audit.pop('physical_cycle')
            flow.audit_committed(0,audit,'d'*64)
            self.assertEqual(journal.snapshot(cycle)[0]['block'],'d'*64)

    def test_only_conflicting_unfinished_local_reservation_is_released(self):
        with tempfile.TemporaryDirectory() as folder:
            flow,journal,cycle,audit=self.fixture(folder)
            other=dict(request_id='e'*64,slot=1,attempt=0,ranges=[dict(start='200',end='300')])
            journal.bind(cycle,1,other);journal.committed(cycle,1,other,'f'*64)
            ident=hashlib.sha256(canonical_bytes(dict(domain='PGN_HORIZON_AUDIT_VIEW_V1',request=audit['request_id'],slot=audit['slot'],attempt=audit['attempt']))).hexdigest()
            flow.cycles.extra_audit_cycle=lambda value:cycle if value==ident else None
            journal.completed_extra(cycle,ident,'1'*64)
            flow.reconcile_physical_extra_bindings()
            self.assertIsNone(journal.snapshot(cycle)[0])
            self.assertEqual(journal.snapshot(cycle)[1]['block'],'f'*64)
            self.assertEqual(journal.completed_extras(cycle),[ident])
            before=journal.path.read_bytes();flow.reconcile_physical_extra_bindings();self.assertEqual(journal.path.read_bytes(),before)
            recovered=CycleExecution(folder,'a'*64)
            self.assertIsNone(recovered.snapshot(cycle)[0]);self.assertEqual(recovered.completed_extras(cycle),[ident])

if __name__=='__main__':unittest.main()
