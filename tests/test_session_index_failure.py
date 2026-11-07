"""Real durable journal/index failures and cold replay; no devices or network."""
from contextlib import closing
import hashlib
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import pgn_session as session
from pgn_native_contract import fields


class IndexFailureTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder=Path(self.tmp.name)/'session'
        first=1<<66
        config=dict(start=str(first),end=str(first+10000),bins=1,report_top=10,**fields(first,first+10000))
        self.store=session.SessionStore.create(self.folder,config,{'uuid':'NO-DEVICE-QA'})
        self.addCleanup(lambda:self.store.close())

    def append(self):
        self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0)

    def digest(self):
        b=self.store.journal.read_bytes()
        return len(b),hashlib.sha256(b).hexdigest()

    def reopen(self):
        self.store.close()
        self.store=session.SessionStore.load(self.folder)

    def assert_blocked(self):
        before=self.digest()
        with self.assertRaises(session.SessionIndexRecoveryRequired):
            self.append()
        self.assertEqual(self.digest(),before)

    def test_index_error_keeps_one_record_and_cold_replay_applies_once(self):
        prior=self.store.sequence
        with patch.object(self.store.gaps,'apply',side_effect=sqlite3.OperationalError('controlled index fault')):
            with self.assertRaises(sqlite3.OperationalError):self.append()
        self.assertEqual(self.store.sequence,prior)
        self.assertEqual(self.store.gaps.meta('journal_sequence'),prior)
        self.assert_blocked()
        self.reopen()
        self.assertEqual(self.store.sequence,prior+1)
        self.assertEqual(self.store.gaps.meta('state'),'PAUSED')
        self.assertEqual(len(self.store.journal.read_bytes().splitlines()),prior+1)
        before=self.digest();self.reopen()
        self.assertEqual(self.digest(),before)
        self.assertEqual(self.store.sequence,prior+1)

    def test_real_sqlite_full_preserves_event_and_replays_after_reopen(self):
        prior=self.store.sequence
        pages=self.store.gaps.db.execute('PRAGMA page_count').fetchone()[0]
        self.store.gaps.db.execute(f'PRAGMA max_page_count={pages}')
        chunk=dict(index=0,large_qa_payload='x'*65536)
        with self.assertRaises(sqlite3.DatabaseError)as caught:
            self.store.append('chunk_closed',chunk=chunk)
        self.assertEqual(caught.exception.sqlite_errorcode,sqlite3.SQLITE_FULL)
        self.assertEqual(self.store.gaps.meta('journal_sequence'),prior)
        self.assertEqual(self.store.gaps.db.execute('SELECT count(*) FROM chunks').fetchone()[0],0)
        self.assert_blocked()
        self.reopen()
        self.assertEqual(self.store.sequence,prior+1)
        self.assertEqual(self.store.gaps.meta('completed_chunk_count'),1)
        self.assertEqual(json.loads(self.store.gaps.db.execute('SELECT value FROM chunks').fetchone()[0]),chunk)
        self.assertEqual(self.store.gaps.db.execute('PRAGMA integrity_check').fetchone()[0],'ok')

    def test_gap_is_recovered_once_without_statistics_double_credit(self):
        low=1<<66
        with patch.object(self.store.gaps,'apply',side_effect=sqlite3.OperationalError('controlled full')):
            with self.assertRaises(sqlite3.OperationalError):self.store.record_gap(low,low+1000,'QA')
        self.assert_blocked()
        self.reopen()
        self.assertEqual(self.store.gaps.get(low),low+1000)
        self.assertEqual(len(self.store.gaps),1)
        before=self.digest()
        self.assertFalse(self.store.record_gap(low,low+1000,'QA'))
        self.assertEqual(self.digest(),before)
        self.assertEqual(len(self.store.gaps),1)

    def test_fsync_failure_blocks_retry_and_complete_record_replays(self):
        prior=self.store.sequence
        with patch.object(session.os,'fsync',side_effect=OSError('controlled sync fault')):
            with self.assertRaises(OSError):self.append()
        self.assert_blocked()
        self.reopen()
        self.assertEqual(self.store.sequence,prior+1)
        self.assertEqual(self.store.gaps.meta('state'),'PAUSED')

    def test_partial_write_retains_torn_bytes_and_recovers_prefix(self):
        prior=self.store.sequence
        real_open=Path.open
        target=self.store.journal
        class Broken:
            def __init__(inner):inner.real=real_open(target,'ab')
            def __enter__(inner):return inner
            def __exit__(inner,*args):inner.real.close()
            def __getattr__(inner,name):return getattr(inner.real,name)
            def write(inner,data):
                inner.real.write(data[:len(data)//2]);inner.real.flush()
                raise OSError('controlled short write')
        def open_override(path,*args,**kwargs):
            return Broken() if path==target and args and args[0]=='ab' else real_open(path,*args,**kwargs)
        with patch.object(Path,'open',new=open_override):
            with self.assertRaises(OSError):self.append()
        self.assert_blocked()
        self.reopen()
        self.assertEqual(self.store.sequence,prior)
        backups=list(self.folder.glob('journal-interrupted-*.bin'))
        self.assertEqual(len(backups),1)
        self.assertGreater(backups[0].stat().st_size,0)
        self.append()
        self.assertEqual(self.store.sequence,prior+1)

    def test_replay_does_not_advance_memory_sequence_before_index_commit(self):
        prior=self.store.sequence
        with patch.object(self.store.gaps,'apply',side_effect=sqlite3.OperationalError('controlled initial fault')):
            with self.assertRaises(sqlite3.OperationalError):self.append()
        self.store.close()
        key=self.store._writer_key
        seen=[]
        def fail(index,event,offset):
            seen.append(session._writers[key].sequence)
            raise sqlite3.OperationalError('controlled replay fault')
        with patch.object(session.GapIndex,'apply',new=fail):
            with self.assertRaises(sqlite3.OperationalError):session.SessionStore.load(self.folder)
        self.assertEqual(seen,[prior])
        self.assertNotIn(key,session._writers)
        self.store=session.SessionStore.load(self.folder)
        self.assertEqual(self.store.sequence,prior+1)

    def test_prewrite_budget_rejection_does_not_poison_writer(self):
        before=self.digest()
        with patch.object(session,'MAX_SESSION_JOURNAL_BYTES',before[0]):
            with self.assertRaises(session.SessionJournalBudgetExceeded):self.append()
        self.assertEqual(self.digest(),before)
        self.append()
        self.assertFalse(self.store._index_recovery_required)

    def test_invalid_prewrite_event_does_not_poison_writer(self):
        before=self.digest()
        with self.assertRaises(ValueError):self.store.append('state',sequence=42)
        self.assertEqual(self.digest(),before)
        self.append()
        self.assertFalse(self.store._index_recovery_required)


if __name__=='__main__':unittest.main()
