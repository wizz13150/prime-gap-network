"""The real publication loop must not monopolize audit admission during reads."""
from pathlib import Path
from types import SimpleNamespace
import sys, threading, unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol')]
from horizon_mining_flow import MiningFlow
from horizon_cpu_bridge import LogicalMinerCpuResources


class NetworkHandoffTests(unittest.TestCase):
    def test_slow_owner_read_does_not_hold_the_network_publication_lock(self):
        entered, release, stopping = threading.Event(), threading.Event(), threading.Event()
        lock = threading.RLock()
        def slow_read():
            entered.set()
            if not release.wait(3):
                raise RuntimeError('Fixture did not release the read')
        flow = MiningFlow.__new__(MiningFlow)
        flow.worker = SimpleNamespace(quitting=stopping, network_lock=lock, network_notice=None,
                                      audits=SimpleNamespace(reveal_owner_reports=slow_read, reveal_committed=lambda: None))
        flow.audit_lanes = []
        flow.cycles = SimpleNamespace(reorg_held=lambda: [])
        flow.outbox = SimpleNamespace(first=lambda: None)
        flow.drain_recovered_report = lambda: None
        flow.changed = threading.Event()
        thread = threading.Thread(target=flow.publish_reports, daemon=True)
        thread.start()
        acquired = False
        try:
            self.assertTrue(entered.wait(2))
            acquired = lock.acquire(timeout=.2)
            self.assertTrue(acquired, 'A read-only historical sweep blocked unrelated audit publication')
        finally:
            if acquired:
                lock.release()
            stopping.set()
            release.set()
            flow.changed.set()
            thread.join(2)
        self.assertFalse(thread.is_alive())

    def test_scheduled_GPU_work_preserves_enabled_CPU_resource_accounting(self):
        calls = []
        gpu = SimpleNamespace(run_scheduled_ticket=lambda *args: calls.append(('scheduled', args)),
                              run_ticket=lambda *args: calls.append(('gpu', args)))
        source = LogicalMinerCpuResources(gpu, Path('unused'), lambda event: None)
        source.lanes['CPU/TEST/0'] = SimpleNamespace(enabled=True)
        source.head = SimpleNamespace(run_ticket=lambda *args: calls.append(('mixed', args)))
        callback = lambda: True
        source.run_scheduled_ticket({'ticket_id': 'a'}, 4, callback)
        self.assertEqual(calls, [('mixed', ({'ticket_id': 'a'}, 4))])

    def test_disabled_CPU_resources_allow_cooperative_GPU_service(self):
        calls = []
        gpu = SimpleNamespace(run_scheduled_ticket=lambda *args: calls.append(args))
        source = LogicalMinerCpuResources(gpu, Path('unused'), lambda event: None)
        source.lanes['CPU/TEST/0'] = SimpleNamespace(enabled=False)
        source.head = SimpleNamespace(run_ticket=lambda *args: self.fail('Disabled CPU lane selected'))
        callback = lambda: True
        source.run_scheduled_ticket({'ticket_id': 'a'}, 4, callback)
        self.assertEqual(calls, [({'ticket_id': 'a'}, 4, callback)])


if __name__ == '__main__':
    unittest.main()
