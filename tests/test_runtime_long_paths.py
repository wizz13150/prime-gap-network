"""Real long-path journal/SQLite IO; no GPU, wallet or network required."""
from pathlib import Path
from contextlib import contextmanager, closing
import json
import hashlib
import os
import sqlite3
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'src/engine/runtime_host'), str(ROOT/'src/protocol')]
from pgn_paths import state_path, windows_extended_path, readonly_sqlite_uri
from pgn_session import SessionStore, SessionControllerLock, atomic_bytes, folder_lock, read_session_metadata
from pgn_native_contract import fields
from horizon_failure_trace import failure_context


@contextmanager
def locked_session(folder):
    lock = SessionControllerLock(folder)
    try: yield
    finally: lock.release()


class RuntimeLongPathTests(unittest.TestCase):
    def test_windows_drive_unc_and_idempotence(self):
        cases = [(r'C:\data\one\..\two', r'\\?\C:\data\two'),
                 (r'\\server\share\one\..\two', r'\\?\UNC\server\share\two'),
                 (r'\\server\share', r'\\?\UNC\server\share')]
        for original, expected in cases:
            self.assertEqual(windows_extended_path(original), expected)
            self.assertEqual(windows_extended_path(expected), expected)
        for invalid in [r'local\state', r'C:state', r'\\.\pipe\state', r'\\?\GLOBALROOT\Device\state']:
            with self.assertRaises(ValueError): windows_extended_path(invalid)

    def test_long_state_commit_reopen_and_readonly_snapshot(self):
        with tempfile.TemporaryDirectory() as tmp:
            # Full filename exceeds MAX_PATH; every component remains legal.
            logical = Path(tmp)/('a'*100)/('b'*100)/('c'*70)/'tickets'/('d'*64)
            folder = state_path(logical)
            folder.mkdir(parents=True)
            self.assertGreater(len(str(logical/'session-index.sqlite3')), 260)
            self.assertIs(folder_lock(logical), folder_lock(folder))
            marker = folder/'runtime-startup.json'
            atomic_bytes(marker, b'{"phase":"loading_profile"}\n')
            self.assertEqual(marker.read_bytes(), b'{"phase":"loading_profile"}\n')
            first = 1 << 67
            cfg = dict(start=str(first), end=str(first+10000), bins=1, report_top=10,
                       **fields(first, first+10000))
            with locked_session(folder):
                store = SessionStore.create(logical, cfg, {'uuid':'NO-DEVICE-QA'})
                try:
                    store.record_gap(first, first+1000, 'QA')
                    store.append('state', state='PAUSED', active_seconds=0, paused_seconds=0)
                    sequence = store.sequence
                    # Read the committed WAL without closing the writer first.
                    self.assertEqual(read_session_metadata(logical)['state'], 'PAUSED')
                    with self.assertRaises(RuntimeError):
                        SessionStore.load(folder)
                finally:
                    store.close()
                reopened = SessionStore.load(logical)
                try:
                    self.assertEqual(reopened.gaps.get(first), first+1000)
                    self.assertEqual(len(reopened.gaps), 1)
                    self.assertEqual(reopened.sequence, sequence)
                    self.assertEqual(reopened.gaps.db.execute('PRAGMA integrity_check').fetchone()[0], 'ok')
                finally:
                    reopened.close()
            file = folder/'session-index.sqlite3'
            with closing(sqlite3.connect(readonly_sqlite_uri(file), uri=True)) as db:
                with self.assertRaises(sqlite3.OperationalError): db.execute('DELETE FROM meta')
            missing = folder/'missing?%# é.sqlite3'
            with self.assertRaises(sqlite3.OperationalError):
                sqlite3.connect(readonly_sqlite_uri(missing), uri=True)
            self.assertFalse(missing.exists())

    def test_sqlite_filename_encoding_and_failure_phase(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = state_path(Path(tmp)/('a'*110)/('b'*110)/'space % # é')
            folder.mkdir(parents=True)
            file = folder/'data % # é.sqlite3'
            with closing(sqlite3.connect(str(file))) as db:
                db.execute('CREATE TABLE exact(value TEXT)')
                db.execute('INSERT INTO exact VALUES (?)', ('153000000000000000001',))
                db.commit()
            with closing(sqlite3.connect(readonly_sqlite_uri(file), uri=True)) as db:
                self.assertEqual(db.execute('SELECT value FROM exact').fetchone()[0], '153000000000000000001')
            atomic_bytes(folder/'runtime-startup.json', json.dumps(dict(
                schema='PGN_CONTROLLER_STARTUP_V1', pid=os.getpid(), phase='verifying_core')).encode())
            value = failure_context(OSError(28, 'disk full'), state_root=folder)
            self.assertEqual(value['step'], 'verifying_core')
            self.assertEqual(json.loads((folder/'runtime-failure.json').read_bytes()), value)

    def test_scientific_archive_conserves_long_path_observation(self):
        import horizon_scientific_gaps as science
        from horizon_archive_repair import primary_pairs
        with tempfile.TemporaryDirectory() as tmp:
            folder = state_path(Path(tmp)/('a'*110)/('b'*110)/('e'*64))
            folder.mkdir(parents=True)
            first = (1 << 67)+1
            cfg = dict(ticket_id=folder.name, start=str(first), end=str(first+10000),
                       bins=100, **fields(first,first+10000))
            with closing(sqlite3.connect(str(folder/'session-index.sqlite3'))) as db:
                db.executescript('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT);CREATE TABLE gaps(lo BLOB PRIMARY KEY,hi BLOB,gap INTEGER,seq INTEGER,bin INTEGER,visible INTEGER,count_ok INTEGER,detail_ok INTEGER);')
                meta = dict(binding=dict(configuration_hash=hashlib.sha256(science.canonical(cfg)).hexdigest()),
                            progress=dict(positions=10000), projection=dict(prefix=str(first+10000)))
                db.executemany('INSERT INTO meta VALUES(?,?)', [(k,json.dumps(v)) for k,v in meta.items()])
                db.execute('INSERT INTO gaps VALUES(?,?,?,?,?,?,?,?)',
                           (first.to_bytes(16,'big'), (first+1000).to_bytes(16,'big'),1000,1,0,1,1,1))
                db.commit()
            descriptor = science.create(folder,cfg)
            self.assertEqual(descriptor['records'],1)
            self.assertEqual(science.verify(folder,descriptor,cfg),descriptor)
            self.assertEqual(primary_pairs(folder,cfg),((first,first+1000),))


if __name__ == '__main__': unittest.main()
