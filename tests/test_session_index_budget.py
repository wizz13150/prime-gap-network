"""Main SQLite page cap and compatibility; actual databases, no devices."""
from contextlib import closing
import json
from pathlib import Path
import sqlite3
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import pgn_index as index
from pgn_native_contract import fields


class IndexBudgetTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.path=Path(self.tmp.name)/'index.sqlite3'
        first=1<<66
        self.config=dict(start=str(first),end=str(first+10000),bins=1,report_top=10,**fields(first,first+10000))

    def open(self):return index.GapIndex(self.path,'a'*32,self.config,'b'*64)

    def test_default_budget_is_enforced_in_actual_connection(self):
        with closing(self.open())as store:
            size=store.db.execute('PRAGMA page_size').fetchone()[0]
            maximum=store.db.execute('PRAGMA max_page_count').fetchone()[0]
            self.assertEqual(maximum,index.MAX_SESSION_INDEX_BYTES//size)
            self.assertLessEqual(maximum*size,128*1024*1024)

    def test_actual_database_page_sizes_use_same_byte_bound(self):
        for size in (512,1024,4096,65536):
            path=Path(self.tmp.name)/f'pages-{size}.sqlite3'
            with closing(sqlite3.connect(path))as db:
                db.execute(f'PRAGMA page_size={size}')
                with patch.object(index,'MAX_SESSION_INDEX_BYTES',1024*1024+size-1):
                    maximum=index.index_page_budget(db)
                self.assertEqual(db.execute('PRAGMA page_size').fetchone()[0],size)
                self.assertEqual(maximum,1024*1024//size)

    def test_oversized_existing_database_refuses_writer_but_is_readable(self):
        store=self.open()
        with store.db:store.put_meta('retained-evidence','x'*65536)
        size=store.db.execute('PRAGMA page_size').fetchone()[0]
        pages=store.db.execute('PRAGMA page_count').fetchone()[0]
        store.close()
        with patch.object(index,'MAX_SESSION_INDEX_BYTES',(pages-1)*size):
            with self.assertRaises(index.SessionIndexBudgetExceeded):self.open()
        with closing(sqlite3.connect(self.path.as_uri()+'?mode=ro',uri=True))as db:
            value=db.execute("SELECT v FROM meta WHERE k='retained-evidence'").fetchone()[0]
            self.assertEqual(json.loads(value),'x'*65536)
            self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
        with closing(self.open())as reopened:
            self.assertEqual(reopened.meta('retained-evidence'),'x'*65536)

    def test_real_sqlite_full_rolls_back_at_exact_page_cap(self):
        with closing(self.open())as store:
            with store.db:store.put_meta('retained-evidence','kept')
            size=store.db.execute('PRAGMA page_size').fetchone()[0]
            pages=store.db.execute('PRAGMA page_count').fetchone()[0]
            with patch.object(index,'MAX_SESSION_INDEX_BYTES',pages*size):index.index_page_budget(store.db)
            with self.assertRaises(sqlite3.DatabaseError)as caught:
                with store.db:store.put_meta('too-large','z'*65536)
            self.assertEqual(caught.exception.sqlite_errorcode,sqlite3.SQLITE_FULL)
            self.assertIsNone(store.meta('too-large'))
            self.assertEqual(store.meta('retained-evidence'),'kept')
            self.assertEqual(store.db.execute('PRAGMA page_count').fetchone()[0],pages)
            self.assertEqual(store.db.execute('PRAGMA integrity_check').fetchone()[0],'ok')

    def test_constructor_failure_closes_connection_and_retry_works(self):
        with patch.object(index,'MAX_SESSION_INDEX_BYTES',1):
            with self.assertRaises(index.SessionIndexBudgetExceeded):self.open()
        moved=self.path.with_suffix('.closed')
        self.path.replace(moved);moved.replace(self.path)
        with closing(self.open())as store:
            self.assertEqual(store.meta('binding')['session_id'],'a'*32)

    def test_binding_failure_also_closes_connection(self):
        self.open().close()
        with self.assertRaisesRegex(ValueError,'binding mismatch'):
            index.GapIndex(self.path,'c'*32,self.config,'b'*64)
        moved=self.path.with_suffix('.closed')
        self.path.replace(moved);moved.replace(self.path)
        with closing(self.open())as store:
            self.assertEqual(store.meta('binding')['session_id'],'a'*32)


if __name__=='__main__':unittest.main()
