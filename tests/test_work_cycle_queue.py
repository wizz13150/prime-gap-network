"""Real resident scheduler and cycle journal, controlled engine/publication facts."""
from pathlib import Path
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/protocol'))
sys.path.insert(0, str(ROOT / 'src/engine/runtime_host'))
from horizon_gpu_queue import ResidentGpuQueue
from horizon_work_cycle import WorkCycleJournal


class CycleQueueTests(unittest.TestCase):
    def test_two_five_partition_audits_precede_search_despite_reversed_preparation(self):
        with tempfile.TemporaryDirectory() as directory, WorkCycleJournal(Path(directory), 'a' * 64) as journal:
            request = '1' * 64
            work = dict(genesis='a' * 64, request_id=request, ticket='2' * 64,
                        ticket_number='53', start='150000000000000000000', length='200000000000000',
                        target_numerator='29', target_denominator='1')
            journal.prepare(work)
            for index in range(2):
                journal.attach(request, index, dict(stage='assigned', request=str(index + 3) * 64, slot=index, attempt=0))
            enabled, stopping = threading.Event(), threading.Event()
            enabled.set(); executed = []; errors = []
            class Engine:
                def run_ticket(self, ticket, limit):
                    executed.append(ticket['tag'])
                    return dict(state='COMPLETE')
            def gate(job):
                return journal.may_search(request) if job.context['role'] == 'search' else journal.may_audit(request, job.context['audit'])
            def start(job):
                if job.context['role'] == 'search': journal.advance(request, 'running')
            def audit(index, part):
                queue.submit(dict(ticket_id=f'{100 + index * 10 + part:064x}', tag=f'audit{index + 1}.{part}'),
                             dict(role='audit', audit=index, part=part), 1)
            def done(job, result):
                if job.context['role'] == 'search':
                    journal.advance(request, 'completed'); stopping.set()
                elif job.context['part'] < 4:
                    audit(job.context['audit'], job.context['part'] + 1)
                else:
                    index = job.context['audit']
                    journal.published(request, index, str(index + 3) * 64, index, 0, str(index + 5) * 64)
            queue = ResidentGpuQueue(Engine(), enabled, stopping, start, done, lambda: None,
                                     lambda error: (errors.append(str(error)), stopping.set()), may_start=gate)
            queue.submit(dict(ticket_id=work['ticket'], tag='search'), dict(role='search'), 1)
            audit(1, 0); audit(0, 0)
            queue.run()
            self.assertFalse(errors)
            self.assertEqual(executed, [f'audit1.{i}' for i in range(5)] + [f'audit2.{i}' for i in range(5)] + ['search'])
            self.assertEqual(journal.snapshot(request)['stage'], 'completed')

    def test_blocked_head_search_is_retained_and_never_skipped(self):
        queue = ResidentGpuQueue(None, threading.Event(), threading.Event(), None, None, None, None,
                                 may_start=lambda job: job.ticket['ticket_id'] != 'a' * 64)
        first = queue.submit(dict(ticket_id='a' * 64), dict(role='search'), 1)
        second = queue.submit(dict(ticket_id='b' * 64), dict(role='search'), 1)
        self.assertIsNone(queue._next())
        self.assertTrue(queue.search_waiting_for_audits)
        self.assertFalse(first.done()); self.assertFalse(second.done())
        self.assertEqual(len(queue.search), 2)
        queue.may_start = lambda job: True
        self.assertEqual(queue._next().ticket['ticket_id'], 'a' * 64)

    def test_invalid_workflow_state_stops_before_engine_execution(self):
        enabled, stopping = threading.Event(), threading.Event(); enabled.set()
        errors = []; starts = []
        queue = ResidentGpuQueue(None, enabled, stopping, lambda job: starts.append(job), None, lambda: None,
                                 lambda error: (errors.append(str(error)), stopping.set()), may_start=lambda job: None)
        queue.submit(dict(ticket_id='a' * 64), dict(role='search'), 1)
        queue.run()
        self.assertFalse(enabled.is_set()); self.assertFalse(starts)
        self.assertEqual(len(errors), 1)


if __name__ == '__main__': unittest.main()
