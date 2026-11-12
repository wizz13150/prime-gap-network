"""The scheduler spends one allowance on activation, never on eligibility scans."""
from collections import deque
from contextlib import ExitStack
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/protocol'), str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'tests')]
from horizon_mining_flow import MiningFlow
from horizon_work_cycle import WorkCycleJournal
from test_work_cycle import work, task


class CatchupBudgetFlowTests(unittest.TestCase):
    def test_scanning_partitions_restart_and_next_search_keep_the_whole_audit_budget(self):
        with tempfile.TemporaryDirectory() as directory, ExitStack() as cleanup:
            root = Path(directory)
            journal = WorkCycleJournal(root / 'work-cycles', 'a' * 64)
            cleanup.callback(journal.close)
            current = work()
            journal.prepare(current)
            for index in range(2):
                ref = task(200 + index, index)
                journal.attach(current['request_id'], index, ref)
                journal.published(current['request_id'], index, ref['request'], index, 0, f'{300 + index:064x}')
            journal.advance(current['request_id'], 'running')
            worker = SimpleNamespace(root=root, current=current, device_lease=object(),
                                     source=SimpleNamespace(), audits=SimpleNamespace())
            flow = MiningFlow.__new__(MiningFlow)
            flow.worker, flow.cycles = worker, journal
            flow.prior_active, flow.view_sequence = {}, 0
            search = SimpleNamespace(context=dict(work_cycle=current['request_id']))
            flow.gpu = SimpleNamespace(search=deque([search]))
            def audit(number, part=0):
                return SimpleNamespace(context=dict(role='audit', work_cycle=None,
                    presentation=dict(audit_id=f'{number:064x}', part_index=part)),
                    ticket=dict(ticket_id=f'{number * 10 + part:064x}'), view_sequence=None)
            first, second, excess = audit(700), audit(701), audit(702)
            for _ in range(10): self.assertTrue(flow.may_start_work(first))
            self.assertNotIn('extra_audits', journal.snapshot(current['request_id']))
            flow.activate(first)
            for part in range(1, 5):
                item = audit(700, part)
                self.assertTrue(flow.may_start_work(item)); flow.activate(item)
            self.assertFalse(flow.may_start_work(second))
            with self.assertRaises(BufferError):flow.activate(second)
            self.assertFalse(flow.may_start_work(excess))
            # The next queued search cannot retag an already admitted audit.
            next_work = work(2); journal.prepare(next_work)
            flow.gpu.search = deque([SimpleNamespace(context=dict(work_cycle=next_work['request_id']))])
            self.assertEqual(flow.extra_audit_owner(audit(700, 4)), current['request_id'])
            self.assertTrue(flow.may_start_work(excess))
            # A prefetch must not lend its fresh quota to the running search.
            flow.gpu.active = SimpleNamespace(context=dict(role='search', work_cycle=current['request_id']))
            self.assertFalse(flow.may_start_work(excess))
            flow.gpu.active = None
            journal.close(); flow.cycles = WorkCycleJournal(root / 'work-cycles', 'a' * 64)
            cleanup.callback(flow.cycles.close)
            flow.gpu.search = deque([search])
            self.assertFalse(flow.may_start_work(excess))
            self.assertTrue(flow.may_start_work(audit(700, 4)))
            flow.cycles.close()


if __name__ == '__main__': unittest.main()
