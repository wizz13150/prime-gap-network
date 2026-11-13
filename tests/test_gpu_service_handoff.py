"""Real queue and durable checkpoint path; controlled CPU stand-ins, no GPU."""
from pathlib import Path
from types import SimpleNamespace
from contextlib import nullcontext
import sys
import threading
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol'), str(ROOT / 'tests')]
from horizon_gpu_queue import ResidentGpuQueue
from horizon_engine_host import EngineService
import horizon_engine_host as host
import pgn_session
import test_output_spool_binding as spool_fixture


class ServiceHandoffTests(unittest.TestCase):
    def arrival(self, cooperative):
        enabled, stopping = threading.Event(), threading.Event()
        enabled.set()
        clock, progress, arrivals, errors, paused, completed, starts = [0], [0], [], [], [], [], []
        futures = {}

        class Engine:
            def compute(self, ticket, maximum, should_yield=lambda: False):
                if ticket['ticket_id'] == 'audit':
                    arrivals.append(clock[0])
                    self_test.assertFalse(futures['search'].done()) if cooperative else None
                    clock[0] += 2_000_000_000
                    return {'state': 'COMPLETE'}
                if cooperative:
                    self_test.assertEqual(maximum, 4 * 10**12)
                while progress[0] < 2000:
                    clock[0] += 100_000_000
                    progress[0] += 1
                    if progress[0] == 5:
                        futures['audit'] = q.submit({'ticket_id': 'audit'}, {'role': 'audit'}, 2)
                    if progress[0] < 2000 and progress[0] % 10 == 0 and should_yield():
                        return {'state': 'YIELDED', 'ticket_id': ticket['ticket_id']}
                return {'state': 'COMPLETE'}

        engine = Engine()
        engine.run_ticket = engine.compute
        if cooperative:
            engine.run_scheduled_ticket = engine.compute
        self_test = self

        def done(job, result):
            completed.append(job.ticket['ticket_id'])
            if len(completed) == 2:
                stopping.set()

        def start(job):
            self.assertIsNone(job.view_sequence)
            job.view_sequence = len(starts) + 1
            starts.append((job.ticket['ticket_id'], job.view_sequence, job.future))

        q = ResidentGpuQueue(engine, enabled, stopping, start, done,
                             lambda: paused.append(True), errors.append,
                             clock_ns=lambda: clock[0])
        futures['search'] = q.submit({'ticket_id': 'search'}, {'role': 'search'}, 64 * 10**12)
        q.run()
        self.assertEqual(errors, [])
        self.assertEqual(paused, [])
        self.assertEqual(progress[0], 2000)
        self.assertEqual(sorted(completed), ['audit', 'search'])
        self.assertTrue(all(f.done() and f.result()['state'] == 'COMPLETE' for f in futures.values()))
        if cooperative:
            self.assertEqual([(name, seq) for name, seq, _ in starts], [('search', 1), ('audit', 2), ('search', 3)])
            self.assertIs(starts[0][2], starts[2][2])
        return arrivals[0]

    def test_late_ready_audit_does_not_wait_for_the_whole_search(self):
        self.assertEqual(self.arrival(False), 200_000_000_000)
        self.assertEqual(self.arrival(True), 1_000_000_000)

    def test_not_admitted_or_user_paused_work_does_not_request_a_handoff(self):
        enabled, stopping = threading.Event(), threading.Event()
        enabled.set()
        now = [0]
        q = ResidentGpuQueue(None, enabled, stopping, None, None, None, None,
                             clock_ns=lambda: now[0], may_start=lambda job: job.context.get('allowed', True))
        q.submit({'ticket_id': 'search'}, {'role': 'search'}, 10)
        search = q._next()
        q.submit({'ticket_id': 'audit'}, {'role': 'audit', 'allowed': False}, 10)
        now[0] = 2_000_000_000
        self.assertFalse(q.should_yield(search, 0))
        q.audits[0].context['allowed'] = True
        self.assertTrue(q.should_yield(search, 0))
        enabled.clear()
        self.assertFalse(q.should_yield(search, 0))
        enabled.set()
        stopping.set()
        self.assertFalse(q.should_yield(search, 0))

    def fixture(self, range_length=4):
        fixture = spool_fixture.NormalPauseTests()
        self.addCleanup(fixture.doCleanups)
        store, worker, source, checkpoint, owner = fixture.make_paused(range_length)
        service, created, acknowledgements = fixture.service(store, worker, source, checkpoint)
        return store, source, service, created, acknowledgements

    def test_partial_native_stop_remains_an_ordinary_pause(self):
        store, source, service, created, acknowledgements = self.fixture()
        with patch.object(pgn_session, 'module_metadata', return_value={'source_manifest_sha256': source, 'profile_k': 8}), \
             patch.object(host, 'run_checked', side_effect=AssertionError('GPU diagnostic forbidden')), \
             patch.object(host, 'create_owned', side_effect=AssertionError('Native launch forbidden')):
            for position in (2, 3):
                result = service._run(store, 4, lambda: True)
                self.assertEqual(result['state'], 'PAUSED')
                self.assertEqual((store.meta['positions'], store.meta['tests']), (position, position * 5))
                self.assertTrue(store.meta['current_chunk']['instance']['work_closed'])
                self.assertFalse((store.folder / 'report.json').exists())
        self.assertEqual(acknowledgements, [2, 3])
        self.assertEqual(len(created), 2)

    def test_user_pause_wins_over_scheduler_yield(self):
        store, source, service, created, acknowledgements = self.fixture()
        (service.root / 'pause.request').write_bytes(b'User OFF\n')
        with patch.object(pgn_session, 'module_metadata', return_value={'source_manifest_sha256': source, 'profile_k': 8}):
            self.assertEqual(service._run(store, 4, lambda: True)['state'], 'PAUSED')
        self.assertEqual(acknowledgements, [])
        self.assertEqual(store.meta['positions'], 1)

    def test_handoff_only_after_complete_chunk_with_warm_native_owner(self):
        store, source, service, created, acknowledgements = self.fixture(8)
        create = service.manager.create
        def complete_chunk(command, environment):
            job = create(command, environment)
            job.position = 4
            job.process_exited = lambda: False  # The resident survives the logical job.
            def acknowledge():
                durable = pgn_session.read_session_metadata(store.folder)
                self.assertEqual(durable['state'], 'CHUNK_COMPLETE')
                self.assertIsNone(durable['current_chunk'])
                self.assertEqual(durable['positions'], 4)
                acknowledgements.append(4)
            job.acknowledge_output = acknowledge
            return job
        service.manager.create = complete_chunk
        with patch.object(pgn_session, 'module_metadata', return_value={'source_manifest_sha256': source, 'profile_k': 8}):
            result = service._run(store, 4, lambda: True)
        self.assertEqual(result, {'state': 'YIELDED', 'ticket_id': store.meta['configuration']['ticket_id']})
        self.assertEqual((store.meta['positions'], store.meta['tests']), (4, 20))
        self.assertEqual(acknowledgements, [4])
        self.assertFalse((store.folder / 'stop.request').exists())
        self.assertFalse((store.folder / 'report.json').exists())
        self.assertFalse(created[0].process_exited())
        folder = store.folder
        store.close()
        reopened = pgn_session.SessionStore.load(folder)
        self.addCleanup(reopened.close)
        self.assertEqual(reopened.meta['positions'], 4)
        self.assertIsNone(reopened.meta['current_chunk'])

    def test_yielded_ticket_is_not_offered_to_the_completed_archive(self):
        store, source, service, created, acknowledgements = self.fixture()
        offered = []
        service.archives = SimpleNamespace(use_ticket=lambda folder: nullcontext(), offer=offered.append)
        service._run_reserved_ticket = lambda folder, config, maximum, callback: {'state': 'YIELDED'}
        with patch.object(host, 'ticket_configuration', return_value=store.meta['configuration']):
            self.assertEqual(service.run_scheduled_ticket({}, 4, lambda: True)['state'], 'YIELDED')
        self.assertEqual(offered, [])


if __name__ == '__main__':
    unittest.main()
