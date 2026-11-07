"""Authoritative journal budget: real append/index/replay, no worker or GPU."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'src/engine/runtime_host'), str(ROOT/'src/protocol')]
import pgn_session as session
from pgn_native_contract import fields


class JournalBudgetTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.folder = Path(self.tmp.name)/'session'
        first = 1 << 66
        config = dict(start=str(first), end=str(first+10000), bins=1, report_top=10, **fields(first, first+10000))
        self.store = session.SessionStore.create(self.folder, config, {'uuid':'NO-DEVICE-QA'})
        self.addCleanup(lambda: self.store.close())

    def snapshot(self):
        return dict(sequence=self.store.sequence, index_sequence=self.store.gaps.meta('journal_sequence'),
                    offset=self.store.gaps.meta('journal_offset'), state=self.store.gaps.meta('state'),
                    bytes=self.store.journal.stat().st_size,
                    sha256=hashlib.sha256(self.store.journal.read_bytes()).hexdigest())

    def event_size(self, note=''):
        return len((json.dumps(dict(sequence=self.store.sequence+1, type='state', state='PAUSED',
                                    active_seconds=0, paused_seconds=0, note=note), separators=(',',':'))+'\n').encode())

    def append(self, note=''):
        self.store.append('state', state='PAUSED', active_seconds=0, paused_seconds=0, note=note)

    def test_exact_budget_is_committed_next_event_is_rejected_without_mutation(self):
        budget = self.store.journal.stat().st_size+self.event_size()
        with patch.object(session, 'MAX_SESSION_JOURNAL_BYTES', budget):
            self.append()
            before = self.snapshot()
            self.assertEqual(before['bytes'], budget)
            for _ in range(3):
                with self.assertRaises(session.SessionJournalBudgetExceeded) as caught:
                    self.append()
                self.assertEqual(caught.exception.code, 'SESSION_JOURNAL_BUDGET_REACHED')
                self.assertEqual(self.snapshot(), before)

    def test_one_byte_over_is_refused_before_any_progress_is_credited(self):
        before = self.snapshot()
        with patch.object(session, 'MAX_SESSION_JOURNAL_BYTES', before['bytes']+self.event_size()-1):
            with self.assertRaises(session.SessionJournalBudgetExceeded):
                self.append()
        self.assertEqual(self.snapshot(), before)

    def test_utf8_storage_bytes_are_counted(self):
        before = self.snapshot()
        note = 'é'*100
        with patch.object(session, 'MAX_SESSION_JOURNAL_BYTES', before['bytes']+self.event_size(note)-1):
            with self.assertRaises(session.SessionJournalBudgetExceeded):
                self.append(note)
        self.assertEqual(self.snapshot(), before)

    def test_replay_at_budget_and_resumption_with_space_preserve_sequence(self):
        prior_offset = self.store.gaps.meta('journal_offset')
        budget = self.store.journal.stat().st_size+self.event_size()
        with patch.object(session, 'MAX_SESSION_JOURNAL_BYTES', budget):
            self.append()
            expected = self.snapshot()
            # DB has not applied the final fsynced event: startup must replay it.
            with self.store.gaps.db:
                self.store.gaps.put_meta('journal_sequence', 1)
                self.store.gaps.put_meta('journal_offset', prior_offset)
            self.store.close()
            self.store = session.SessionStore.load(self.folder)
            self.assertEqual(self.snapshot(), expected)
            with self.assertRaises(session.SessionJournalBudgetExceeded):
                self.append()
            self.assertEqual(self.snapshot(), expected)
        self.append()
        self.assertEqual(self.store.sequence, expected['sequence']+1)

    def test_existing_oversized_history_is_readable_but_cannot_grow(self):
        self.append()
        before = self.snapshot()
        with patch.object(session, 'MAX_SESSION_JOURNAL_BYTES', before['bytes']-1):
            self.store.close()
            self.store = session.SessionStore.load(self.folder)
            with self.assertRaises(session.SessionJournalBudgetExceeded):
                self.append()
            self.assertEqual(self.snapshot(), before)

    def test_rejected_event_does_not_change_gap_statistics(self):
        before = self.snapshot()
        lower = 1 << 66
        with patch.object(session, 'MAX_SESSION_JOURNAL_BYTES', before['bytes']):
            with self.assertRaises(session.SessionJournalBudgetExceeded):
                self.store.record_gap(lower, lower+1000, 'QA')
        self.assertIsNone(self.store.gaps.get(lower))
        self.assertEqual(self.snapshot(), before)


if __name__ == '__main__':
    unittest.main()
