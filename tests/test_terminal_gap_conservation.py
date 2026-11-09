"""Final observations survive committed process exit, replay and export.

Synthetic integer pairs exercise persistence only, not primality or coverage.
"""
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'tools'), str(P/'src/engine/runtime_host'), str(P/'src/protocol')]
import export_gap_observations as exported
import horizon_scientific_gaps as science
from pgn_native_contract import fields
from pgn_session import SessionStore


class TerminalGapConservationTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.folder = Path(temporary.name)/('1'*64)
        self.start = 150_000_000_000_000_000_000
        self.end = self.start+10**15
        self.last = self.end-3
        self.configuration = dict(ticket_id=self.folder.name, start=str(self.start),
            end=str(self.end), bins=100, report_top=100,
            **fields(self.start,self.end,minimum='1020'))
        self.store = SessionStore.create(self.folder,self.configuration,{'uuid':'NO-DEVICE-TEST'})
        self.addCleanup(lambda:self.store.close())

    def assert_archived_last(self,expected):
        descriptor = science.create(self.folder,self.configuration)
        science.verify(self.folder,descriptor,self.configuration)
        rows = list(exported.rows(self.folder,descriptor,self.configuration))
        self.assertEqual([(int(r[0]),int(r[1]))for r in rows],expected)
        self.assertEqual(descriptor['records'],len(expected))
        self.assertEqual(rows[-1][0],str(self.last))
        self.assertEqual(rows[-1][1],str(self.last+1020))

    def test_final_gap_is_committed_before_close_and_upper_may_cross_end(self):
        expected=[(self.start+1,self.start+1021),(self.last,self.last+1020)]
        for low,high in expected:self.assertTrue(self.store.record_gap(low,high,'TERMINAL_TEST'))
        # A separate read-only connection sees the final row while the writer
        # remains open: close() is not what commits the last observation.
        with closing(sqlite3.connect(self.folder/'session-index.sqlite3'))as reader:
            row=reader.execute('SELECT lo,hi FROM gaps ORDER BY lo DESC LIMIT 1').fetchone()
        self.assertEqual(tuple(int.from_bytes(v,'big')for v in row),expected[-1])
        self.assert_archived_last(expected)

    def test_process_exit_without_close_preserves_final_row(self):
        self.store.close()
        code='''import os,sys
sys.path[:0]=sys.argv[1:3]
from pgn_session import SessionStore
s=SessionStore.load(sys.argv[3]);low=int(sys.argv[4])
assert s.record_gap(low,low+1020,'TERMINAL_CHILD')
os._exit(0)
'''
        child=subprocess.run([sys.executable,'-I','-c',code,
            str(P/'src/engine/runtime_host'),str(P/'src/protocol'),str(self.folder),str(self.last)],
            capture_output=True,timeout=15)
        self.assertEqual(child.returncode,0,child.stderr.decode(errors='replace'))
        self.store=SessionStore.load(self.folder)
        self.assertEqual(self.store.gaps.get(self.last),self.last+1020)
        self.assert_archived_last([(self.last,self.last+1020)])

    def test_last_journal_record_replays_once_after_index_failure(self):
        with patch.object(self.store.gaps,'apply',side_effect=sqlite3.OperationalError('controlled final-row fault')):
            with self.assertRaises(sqlite3.OperationalError):
                self.store.record_gap(self.last,self.last+1020,'TERMINAL_REPLAY')
        self.store.close();self.store=SessionStore.load(self.folder)
        self.assertEqual(self.store.gaps.get(self.last),self.last+1020)
        self.assertFalse(self.store.record_gap(self.last,self.last+1020,'TERMINAL_REPLAY'))
        self.assert_archived_last([(self.last,self.last+1020)])


if __name__=='__main__':unittest.main()
