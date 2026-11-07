"""Real compacted archives across bounded, interrupted maintenance invocations."""
from pathlib import Path
from contextlib import nullcontext
import hashlib
import json
import sqlite3
from contextlib import closing
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol')]
import horizon_ticket_archive as archive
from pgn_session import SessionControllerLock, SessionControllerBusy


class ArchiveBatchTests(unittest.TestCase):
    def test_foreground_guard_defers_candidate_and_advances_fair_cursor(self):
        folder, report = self.ticket(1)
        result = archive.compact_batch(self.root, 1, _ticket_guard=lambda path: nullcontext(False))
        self.assertEqual(result['retained'], 1)
        self.assertEqual(result['next_after'], folder.name)
        self.assertFalse((folder / archive.ARCHIVE_NAME).exists())
        self.assertTrue(all((folder / name).exists() for name in archive.WORK_FILES))
        self.assertEqual(archive.compact_batch(self.root, 1)['compacted'], 1)
        self.assertEqual(archive.load(folder)['report'], report)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tickets = self.root / 'tickets'
        self.tickets.mkdir()
        self.maintenance = self.root / '.retention-maintenance'
        self.addCleanup(patch.stopall)
        patch('horizon_ticket_archive.read_session_metadata', side_effect=self.metadata).start()
        patch('horizon_status_stream.ticket_frame', side_effect=self.frame).start()

    def metadata(self, folder):
        return json.loads((folder / 'session.json').read_bytes())

    def frame(self, folder):
        report = json.loads((folder / 'report.json').read_bytes())['body']
        return dict(ticket_id=folder.name, start=report['start'], end=report['end'],
                    positions=report['positions'], phase='Report ready',
                    scopes=dict(ticket=dict(statistics=dict(count_total='3'))))

    def ticket(self, number, state='COMPLETE'):
        folder = self.tickets / f'{number:064x}'
        folder.mkdir()
        config = dict(ticket_id=folder.name, start='150000000000000000000',
                      end='150000200000000000000', threshold_policy=dict(sha256='a' * 64))
        computed = dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1', ticket_id=folder.name,
                        start=config['start'], end=config['end'], positions='200000000000000',
                        policy_sha256='a' * 64, counts=['3'] + ['0'] * 99, best=[])
        report = dict(body=computed, sha256=hashlib.sha256(archive.canonical(computed)).hexdigest(),
                      network_accepted=False)
        meta = dict(schema='PGN68_DURABLE_SESSION_V1', state=state, current_chunk=None, configuration=config)
        (folder / 'session.json').write_bytes(archive.canonical(meta))
        (folder / 'report.json').write_bytes(archive.canonical(report))
        for name in archive.WORK_FILES:
            (folder / name).write_bytes(b'redundant committed work')
        index=folder/'session-index.sqlite3';index.unlink()
        for suffix in ('-wal','-shm'):(folder/('session-index.sqlite3'+suffix)).write_bytes(b'')
        with closing(sqlite3.connect(index))as db,db:
            db.executescript('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT);CREATE TABLE gaps(lo BLOB PRIMARY KEY,hi BLOB,gap INTEGER,seq INTEGER,bin INTEGER);')
            db.execute('INSERT INTO meta VALUES(?,?)',('binding',json.dumps(dict(configuration_hash=hashlib.sha256(archive.canonical(config)).hexdigest()))))
            start=int(config['start'])
            db.executemany('INSERT INTO gaps VALUES(?,?,?,?,?)',[((start+i*20).to_bytes(16,'big'),(start+i*20+10).to_bytes(16,'big'),10,i,0)for i in range(3)])
        return folder, report

    def test_all_521_tickets_are_reached_in_128_ticket_batches(self):
        originals = [self.ticket(i) for i in range(1, 522)]
        seen = []
        real = archive.compact
        def record(folder, **kwargs):
            seen.append(folder.name)
            return real(folder, **kwargs)
        summaries = []
        with patch('horizon_ticket_archive.compact', side_effect=record):
            for _ in range(5):
                summaries.append(archive.compact_batch(self.root, 128))
        self.assertEqual([s['visited'] for s in summaries], [128, 128, 128, 128, 9])
        self.assertEqual(len(set(seen)), 521)
        self.assertEqual(seen, sorted(seen))
        self.assertEqual(sum(s['deferred'] for s in summaries), 0)
        self.assertGreater(sum(s['reclaimed_bytes'] for s in summaries), 0)
        for folder, report in originals:
            self.assertEqual(archive.load(folder)['report'], report)
            self.assertEqual(json.loads((folder / 'report.json').read_bytes()), report)
            self.assertFalse(any((folder / name).exists() for name in archive.WORK_FILES))

    def test_wrap_revisits_active_and_new_lower_ids(self):
        active, _ = self.ticket(10, 'RUNNING')
        complete, _ = self.ticket(20)
        self.assertEqual(archive.compact_batch(self.root, 1)['retained'], 1)
        self.assertEqual(archive.compact_batch(self.root, 1)['compacted'], 1)
        new, report = self.ticket(1)
        wrapped = archive.compact_batch(self.root, 1)
        self.assertTrue(wrapped['wrapped'])
        self.assertEqual(wrapped['next_after'], new.name)
        self.assertEqual(archive.load(new)['report'], report)
        for name in archive.WORK_FILES:
            self.assertTrue((active / name).exists())
        self.assertEqual(archive.compact_batch(self.root, 1)['retained'], 1)
        self.assertIsNotNone(archive.load(complete))

    def test_crash_before_cursor_commit_repeats_compaction_idempotently(self):
        folder, report = self.ticket(1)
        self.ticket(2)
        real = archive.atomic_bytes
        def fail_cursor(path, data):
            if Path(path).name == 'cursor.json':
                raise OSError('injected cursor interruption')
            return real(path, data)
        with patch('horizon_ticket_archive.atomic_bytes', side_effect=fail_cursor):
            with self.assertRaisesRegex(OSError, 'cursor interruption'):
                archive.compact_batch(self.root, 1)
        self.assertEqual(archive.load(folder)['report'], report)
        before = (folder / archive.ARCHIVE_NAME).read_bytes()
        self.assertEqual(archive.compact_batch(self.root, 1)['next_after'], folder.name)
        self.assertEqual((folder / archive.ARCHIVE_NAME).read_bytes(), before)
        self.assertEqual(archive.compact_batch(self.root, 1)['next_after'], f'{2:064x}')

    def test_deferred_ticket_cannot_starve_later_work(self):
        bad, _ = self.ticket(1)
        good, report = self.ticket(2)
        (bad / 'report.json').write_bytes(b'{}')
        self.assertEqual(archive.compact_batch(self.root, 1)['deferred'], 1)
        self.assertEqual(archive.compact_batch(self.root, 1)['compacted'], 1)
        self.assertEqual(archive.load(good)['report'], report)
        self.assertTrue(all((bad / name).exists() for name in archive.WORK_FILES))
        self.assertEqual(archive.compact_batch(self.root, 1)['deferred'], 1)

    def test_dry_run_does_not_create_cursor_or_alter_ticket(self):
        folder, _ = self.ticket(1)
        before = {p.name: p.read_bytes() for p in folder.iterdir()}
        result = archive.compact_batch(self.root, 1, dry_run=True)
        self.assertEqual(result['compacted'], 1)
        self.assertFalse(self.maintenance.exists())
        for name, data in before.items():
            self.assertEqual((folder / name).read_bytes(), data)

    def test_invalid_or_foreign_cursor_is_rejected_before_compaction(self):
        folder, _ = self.ticket(1)
        self.maintenance.mkdir()
        cursor = self.maintenance / 'cursor.json'
        for after, tickets in [('../escape', str(self.tickets)), ('a' * 64, str(self.root / 'other'))]:
            cursor.write_bytes(archive.canonical(dict(schema=archive.BATCH_CURSOR_SCHEMA,
                                                      tickets=tickets, after=after)))
            with self.assertRaisesRegex(ValueError, 'cursor'):
                archive.compact_batch(self.root, 1)
            self.assertFalse((folder / archive.ARCHIVE_NAME).exists())
        cursor.write_bytes(b'[]')
        with self.assertRaisesRegex(ValueError, 'cursor'):
            archive.compact_batch(self.root, 1)
        self.assertFalse((folder / archive.ARCHIVE_NAME).exists())

    def test_second_maintenance_writer_is_refused(self):
        folder, _ = self.ticket(1)
        self.maintenance.mkdir()
        lock = SessionControllerLock(self.maintenance)
        try:
            with self.assertRaises(SessionControllerBusy):
                archive.compact_batch(self.root, 1)
        finally:
            lock.release()
        self.assertFalse((folder / archive.ARCHIVE_NAME).exists())

    def test_non_ticket_directories_and_files_are_untouched(self):
        ignored = self.tickets / 'not-a-ticket'
        ignored.mkdir()
        (ignored / 'keep.txt').write_bytes(b'foreign data')
        (self.tickets / ('a' * 64)).write_bytes(b'not a directory')
        folder, _ = self.ticket(1)
        self.assertEqual(archive.compact_batch(self.root, 1)['visited'], 1)
        self.assertEqual((ignored / 'keep.txt').read_bytes(), b'foreign data')
        self.assertEqual((self.tickets / ('a' * 64)).read_bytes(), b'not a directory')
        self.assertIsNotNone(archive.load(folder))


if __name__ == '__main__':
    unittest.main()
