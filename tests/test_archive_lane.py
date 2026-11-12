"""Off-lane isolation, bounded hints, fair restart recovery and exact archives; no GPU."""
from pathlib import Path
from types import SimpleNamespace
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol'), str(ROOT / 'tests')]
import horizon_archive_lane as lane_module
from horizon_archive_lane import ArchiveLane
from horizon_engine_host import EngineService
import test_ticket_archive as archive_fixture


class ArchiveLaneTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.root = Path(self.temp.name).resolve()
        (self.root / 'tickets').mkdir()
        self.notes = []
        self.lanes = []

    def tearDown(self):
        for lane in self.lanes:
            self.assertTrue(lane.close(), 'Archive test left a live worker')
        self.temp.cleanup()

    def lane(self, **kwargs):
        value = ArchiveLane(self.root, self.notes.append, **kwargs)
        self.lanes.append(value)
        return value

    def ticket(self, number):
        return self.root / 'tickets' / f'{number:064x}'

    def test_blocked_cleanup_does_not_block_result_return(self):
        entered, release = threading.Event(), threading.Event()
        def blocked(folder):
            entered.set()
            if not release.wait(3):
                raise RuntimeError('Test did not release archive')
        with patch.object(ArchiveLane, '_compact', side_effect=blocked):
            lane = self.lane()
            service = EngineService.__new__(EngineService)
            service.root, service.archives = self.root, lane
            service.gpu_uuid = 'GPU-' + '0' * 36
            store = Mock(meta={'state': 'COMPLETE'})
            report = {'retained': 'exact result'}
            service._run = Mock(return_value=report)
            result, returned = [], threading.Event()
            def calculate():
                result.append(service.run_ticket({}))
                returned.set()
            try:
                with patch('horizon_engine_host.ticket_configuration', return_value={'ticket_id': self.ticket(1).name}), \
                     patch('horizon_engine_host.SessionControllerLock'), \
                     patch('horizon_engine_host.SessionStore.create', return_value=store):
                    store.meta['configuration'] = {'ticket_id': self.ticket(1).name}
                    caller = threading.Thread(target=calculate)
                    caller.start()
                    self.assertTrue(entered.wait(1))
                    self.assertTrue(returned.wait(1), 'Result waited for blocked archive I/O')
                    self.assertFalse(release.is_set())
                    self.assertIs(result[0], report)
                    store.close.assert_called_once()
                    caller.join(1)
            finally:
                release.set()

    def test_duplicate_and_full_hints_are_bounded_and_nonblocking(self):
        entered, release = threading.Event(), threading.Event()
        def blocked(folder):
            entered.set()
            release.wait(3)
        with patch.object(ArchiveLane, '_compact', side_effect=blocked):
            lane = self.lane()
            try:
                self.assertTrue(lane.offer(self.ticket(1)))
                self.assertTrue(entered.wait(1))
                for unused in range(100):
                    self.assertTrue(lane.offer(self.ticket(1)))
                for number in range(2, 10):
                    self.assertTrue(lane.offer(self.ticket(number)))
                self.assertFalse(lane.offer(self.ticket(10)))
                self.assertEqual(len(lane.pending), 8)
                self.assertEqual(len(lane.members), 9)  # Eight hints + one active operation.
            finally:
                release.set()

    def test_restart_sweep_uses_persisted_cursor_without_hints(self):
        swept = threading.Event()
        with patch.object(lane_module, 'compact_batch', side_effect=lambda root, **kwargs: swept.set()) as sweep:
            self.lane(sweep_seconds=.01)
            self.assertTrue(swept.wait(1))
            self.assertEqual(sweep.call_args.kwargs['limit'], 64)
            self.assertEqual(sweep.call_args.kwargs['time_budget_seconds'], .15)
            self.assertTrue(callable(sweep.call_args.kwargs['_ticket_guard']))
            self.assertEqual(sweep.call_args.args, (self.root,))

    def test_archived_dependency_revisits_shared_owner_without_full_sweep(self):
        child, owner = self.ticket(20), self.ticket(1)
        revisited = threading.Event()
        def compact(folder):
            if folder == child:
                return [owner.name]
            if folder == owner:
                revisited.set()
            return []
        with patch.object(ArchiveLane, '_compact', side_effect=compact), \
             patch.object(lane_module, 'compact_batch', return_value={'visited': 0}):
            lane = self.lane(sweep_seconds=3600)
            lane.offer(child)
            self.assertTrue(revisited.wait(2), 'Shared owner waited for a whole catalogue sweep')

    def test_io_error_is_reported_without_killing_lane(self):
        seen = threading.Event()
        def failing(folder):
            seen.set()
            raise OSError('injected archive disk error')
        with patch.object(ArchiveLane, '_compact', side_effect=failing):
            lane = self.lane()
            lane.offer(self.ticket(1))
            self.assertTrue(seen.wait(1))
            with lane.condition:
                lane.condition.wait_for(lambda: bool(self.notes), 1)
            self.assertTrue(lane.thread.is_alive())
            self.assertIn('injected archive disk error', self.notes[0]['message'])

    def test_callback_failure_does_not_kill_lane(self):
        lane = self.lane()
        lane.notice = Mock(side_effect=RuntimeError('presentation gone'))
        lane._report(ValueError('kept on disk'))
        self.assertTrue(lane.thread.is_alive())

    def test_foreign_or_traversing_hints_are_refused(self):
        lane = self.lane()
        for folder in [self.root / ('a' * 64), self.root / 'tickets' / ('A' * 64),
                       self.root / 'tickets' / '..' / ('a' * 64)]:
            with self.assertRaises(ValueError):
                lane.offer(folder)
        self.assertEqual(len(lane.pending), 0)

    def test_close_wakes_idle_worker_and_refuses_new_hints(self):
        lane = self.lane()
        self.assertTrue(lane.close())
        self.assertFalse(lane.offer(self.ticket(1)))
        self.assertTrue(lane.close())

    def test_invalid_interval_does_not_start_worker(self):
        with self.assertRaises(ValueError):
            self.lane(sweep_seconds=0)

    def test_next_ticket_does_not_wait_for_other_ticket_cleanup(self):
        entered, release, next_ready = threading.Event(), threading.Event(), threading.Event()
        def blocked(folder):
            entered.set()
            release.wait(3)
        with patch.object(ArchiveLane, '_compact', side_effect=blocked):
            lane = self.lane()
            try:
                lane.offer(self.ticket(1))
                self.assertTrue(entered.wait(1))
                def begin_next():
                    with lane.use_ticket(self.ticket(2)):
                        next_ready.set()
                caller = threading.Thread(target=begin_next)
                caller.start()
                self.assertTrue(next_ready.wait(1))
                self.assertFalse(release.is_set())
                caller.join(1)
            finally:
                release.set()

    def test_same_ticket_recovery_serializes_with_archive_commit(self):
        entered, release, reserved = threading.Event(), threading.Event(), threading.Event()
        def blocked(folder):
            entered.set()
            release.wait(3)
        with patch.object(ArchiveLane, '_compact', side_effect=blocked):
            lane = self.lane()
            caller = None
            try:
                lane.offer(self.ticket(1))
                self.assertTrue(entered.wait(1))
                def recover_same():
                    with lane.use_ticket(self.ticket(1)):
                        reserved.set()
                caller = threading.Thread(target=recover_same)
                caller.start()
                self.assertFalse(reserved.wait(.05))
                release.set()
                self.assertTrue(reserved.wait(1))
            finally:
                release.set()
                if caller is not None:
                    caller.join(1)

    def test_maintenance_guard_refuses_computing_ticket(self):
        lane = self.lane()
        with lane.use_ticket(self.ticket(1)):
            with lane._ticket_guard(self.ticket(1)) as allowed:
                self.assertFalse(allowed)
            with lane._ticket_guard(self.ticket(2)) as allowed:
                self.assertTrue(allowed)
        self.assertFalse(lane.foreground)

    def test_real_archive_bytes_and_recovery_are_preserved_off_lane(self):
        fixture = archive_fixture.TicketArchiveTests()
        fixture.setUp()
        fixture.valid_index()
        self.addCleanup(fixture.tearDown)
        self.root = fixture.folder.parent.parent.resolve()
        finished = threading.Event()
        actual = lane_module.compact
        def compact(folder):
            result = actual(folder)
            finished.set()
            return result
        with patch('horizon_ticket_archive.read_session_metadata', return_value=fixture.meta), \
             patch('horizon_status_stream.ticket_frame', return_value=fixture.frame), \
             patch.object(lane_module, 'compact', side_effect=compact):
            lane = self.lane()
            lane.offer(fixture.folder)
            self.assertTrue(finished.wait(2),self.notes)
            self.assertEqual(fixture.report,
                             archive_fixture.archive.result(fixture.folder, fixture.cfg))
            self.assertFalse(any((fixture.folder / name).exists()
                                 for name in archive_fixture.archive.WORK_FILES))
            self.assertEqual((fixture.folder / 'report.json').read_bytes(),
                             archive_fixture.archive.canonical(fixture.report))
            self.assertTrue(lane.close())


if __name__ == '__main__':
    unittest.main()
