from pathlib import Path
import json
import sys
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/protocol'),str(ROOT/'src/engine/runtime_host')]
from horizon_cycle_execution import CycleExecution


class PhysicalCycleTests(unittest.TestCase):
    def test_old_native_receipts_cannot_supply_fresh_physical_service(self):
        with tempfile.TemporaryDirectory() as root:
            journal=CycleExecution(root,'a'*64);cycle='b'*64;journal.prepare(cycle,{cycle})
            self.assertFalse(journal.ready(cycle))
            first=dict(request_id='c'*64,slot=0,attempt=0);second=dict(request_id='d'*64,slot=1,attempt=0)
            journal.bind(cycle,0,first);journal.bind(cycle,1,second)
            self.assertTrue(journal.may_audit(cycle,0));self.assertFalse(journal.may_audit(cycle,1))
            journal.committed(cycle,0,first,'e'*64);self.assertFalse(journal.ready(cycle));self.assertTrue(journal.may_audit(cycle,1))
            journal.committed(cycle,1,second,'f'*64);self.assertTrue(journal.ready(cycle))
            journal.completed_extra(cycle,'3'*64,'4'*64)
            resumed=CycleExecution(root,'a'*64);self.assertEqual(resumed.completed_extras(cycle),['3'*64]);self.assertTrue(resumed.ready(cycle));self.assertEqual(resumed.snapshot(cycle),journal.snapshot(cycle))
            next_cycle='1'*64;resumed.prepare(next_cycle,{cycle,next_cycle});self.assertFalse(resumed.ready(next_cycle));self.assertEqual(resumed.completed_extras(next_cycle),[])
            before=resumed.path.read_bytes()
            with self.assertRaises(ValueError):resumed.bind(next_cycle,0,first)
            self.assertEqual(before,resumed.path.read_bytes())
            with self.assertRaises(ValueError):resumed.committed(cycle,0,first,'2'*64)
            first['replay_previous_block']='e'*64;resumed.committed(cycle,0,first,'2'*64)
            self.assertEqual(resumed.snapshot(cycle)[0]['block'],'2'*64)

    def test_only_authenticated_empty_pool_can_skip_and_live_set_does_not_grow(self):
        with tempfile.TemporaryDirectory() as root:
            journal=CycleExecution(root,'a'*64)
            for i in range(40):
                cycle=f'{i+1:064x}';journal.prepare(cycle,{cycle})
                with self.assertRaises(ValueError):journal.unavailable(cycle,0,'bad-anchor')
                journal.unavailable(cycle,0,'b'*64);journal.unavailable(cycle,1,'b'*64)
                self.assertTrue(journal.ready(cycle))
            self.assertEqual(len(journal.cycles),1);self.assertLess(journal.path.stat().st_size,1024)
            with self.assertRaises(ValueError):CycleExecution(root,'c'*64)

    def test_orphaned_assignment_retires_without_mutating_other_physical_results(self):
        with tempfile.TemporaryDirectory() as root:
            journal=CycleExecution(root,'a'*64);cycle='b'*64;journal.prepare(cycle,{cycle})
            first=dict(request_id='c'*64,slot=0,attempt=0);second=dict(request_id='d'*64,slot=1,attempt=0)
            journal.bind(cycle,0,first);journal.bind(cycle,1,second);journal.committed(cycle,0,first,'e'*64)
            journal.retire(cycle,1,second);self.assertFalse(journal.ready(cycle));self.assertEqual(journal.snapshot(cycle)[0]['block'],'e'*64)
            second['attempt']=1;journal.bind(cycle,1,second);journal.committed(cycle,1,second,'f'*64);self.assertTrue(journal.ready(cycle))

if __name__=='__main__':unittest.main()
