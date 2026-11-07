"""Actual pinned readers, WAL frames, backpressure and durable cold recovery."""
from contextlib import closing
from pathlib import Path
import hashlib,json,sqlite3,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import pgn_index as index
import pgn_session as session
from pgn_native_contract import fields


class WalBudgetTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.folder=Path(self.tmp.name)/'session';first=1<<66
        self.config=dict(start=str(first),end=str(first+10000),bins=1,report_top=10,**fields(first,first+10000))
        self.store=session.SessionStore.create(self.folder,self.config,{'uuid':'NO-DEVICE-WAL-QA'})
        self.addCleanup(lambda:self.store.close())
        self.path=self.store.gaps.path

    def reader(self):
        reader=sqlite3.connect(self.path.as_uri()+'?mode=ro',uri=True)
        reader.execute('BEGIN');reader.execute('SELECT v FROM meta LIMIT 1').fetchone()
        return reader

    def append(self):self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0)

    def test_actual_connection_disables_mid_transaction_spills(self):
        self.assertEqual(self.store.gaps.db.execute('PRAGMA cache_spill').fetchone(),(0,))
        size=self.store.gaps.db.execute('PRAGMA page_size').fetchone()[0]
        pages=self.store.gaps.db.execute('PRAGMA max_page_count').fetchone()[0]
        worst=index.MAX_WAL_WRITE_START_BYTES+32+pages*(size+24)
        self.assertLess(worst,167*1024*1024)

    def test_pinned_reader_stops_growth_without_new_journal_record_then_retry(self):
        g=self.store.gaps;g.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        reader=self.reader()
        try:
            with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):
                blocked=False
                for _ in range(100):
                    before=self.store.journal.read_bytes();sequence=self.store.sequence;wal=g.wal_bytes()
                    try:self.append()
                    except index.SessionIndexWalBudgetExceeded as error:
                        self.assertEqual(error.code,'SESSION_INDEX_WAL_BUDGET_REACHED');blocked=True;break
                self.assertTrue(blocked,'A pinned reader must not permit unbounded successive commits')
                self.assertEqual(self.store.journal.read_bytes(),before)
                self.assertEqual(self.store.sequence,sequence)
                self.assertFalse(self.store._index_recovery_required)
                self.assertLessEqual(g.wal_bytes(),wal)
                self.assertEqual(g.meta('journal_sequence'),sequence)
                reader.rollback();reader.close();reader=None
                self.append()
                self.assertEqual(self.store.sequence,sequence+1)
                self.assertEqual(g.db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
            self.store.close();self.store=session.SessionStore.load(self.folder)
            self.assertEqual(self.store.sequence,sequence+1)
            events=[json.loads(line)for line in self.store.journal.read_bytes().splitlines()]
            self.assertEqual([e['sequence']for e in events],list(range(1,sequence+2)))
        finally:
            if reader is not None:reader.close()

    def test_single_large_transaction_writes_each_dirty_page_once(self):
        g=self.store.gaps;g.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        g.db.execute('PRAGMA cache_size=1')
        with patch.object(index,'MAX_SESSION_INDEX_BYTES',1024*1024):
            maximum=index.index_page_budget(g.db)
        reader=self.reader()
        try:
            before=g.wal_bytes()
            with g.write_transaction():
                for i in range(900):
                    g.put_meta('spill-stress-'+str(i%180),str(i)+'x'*2048)
                    if i%50==0:self.assertLessEqual(g.wal_bytes(),before+32,'No WAL frames before commit when spilling is disabled')
            size=g.db.execute('PRAGMA page_size').fetchone()[0]
            after=g.wal_bytes();self.assertGreater(after,65536)
            self.assertEqual((after-32)%(size+24),0)
            frames=(after-32)//(size+24)
            self.assertLessEqual(frames,maximum)
            self.assertLessEqual(after,32+maximum*(size+24))
            self.assertTrue(g.meta('spill-stress-179').startswith('899'))
            self.assertEqual(g.db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
        finally:reader.close()

    def test_intervening_writer_after_journal_preflight_requires_exact_cold_replay(self):
        g=self.store.gaps;g.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        reader=self.reader();other=sqlite3.connect(self.path)
        try:
            reserve=g.reserve_write;once=False;prior=self.store.sequence
            def intervene():
                nonlocal once
                reserve()
                if not once:
                    once=True
                    with other:other.execute('INSERT INTO meta VALUES(?,?)',('foreign-fixture',json.dumps('x'*131072)))
            with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536),patch.object(g,'reserve_write',side_effect=intervene):
                with self.assertRaises(index.SessionIndexWalBudgetExceeded):self.append()
            self.assertTrue(self.store._index_recovery_required)
            saved=self.store.journal.read_bytes()
            with self.assertRaises(session.SessionIndexRecoveryRequired):self.append()
            self.assertEqual(saved,self.store.journal.read_bytes())
            other.close();other=None;reader.close();reader=None
            self.store.close();self.store=session.SessionStore.load(self.folder)
            self.assertEqual(self.store.sequence,prior+1)
            self.assertEqual(self.store.gaps.meta('journal_sequence'),prior+1)
            self.assertEqual(self.store.gaps.meta('state'),'PAUSED')
            self.assertEqual(self.store.gaps.meta('foreign-fixture'),'x'*131072)
        finally:
            if other is not None:other.close()
            if reader is not None:reader.close()

    def test_no_spill_frame_bound_at_extreme_supported_page_sizes(self):
        for size in (512,1024,4096,65536):
            with self.subTest(page_size=size):
                path=Path(self.tmp.name)/f'page-{size}.sqlite3'
                with closing(sqlite3.connect(path))as db:
                    db.execute(f'PRAGMA page_size={size}');db.execute('CREATE TABLE seed(x)');db.commit()
                with patch.object(index,'MAX_SESSION_INDEX_BYTES',2*1024*1024),closing(index.GapIndex(path,'a'*32,self.config,'b'*64))as g:
                    g.db.execute('PRAGMA wal_checkpoint(TRUNCATE)');g.db.execute('PRAGMA cache_size=1')
                    maximum=g.db.execute('PRAGMA max_page_count').fetchone()[0]
                    with closing(sqlite3.connect(path.as_uri()+'?mode=ro',uri=True))as reader:
                        reader.execute('BEGIN');reader.execute('SELECT v FROM meta LIMIT 1').fetchone()
                        with g.write_transaction():
                            for i in range(400):
                                g.put_meta('page-stress-'+str(i%40),str(i)+'x'*900)
                                if i%50==0:self.assertLessEqual(g.wal_bytes(),32)
                        frames=(g.wal_bytes()-32)//(size+24)
                        self.assertEqual((g.wal_bytes()-32)%(size+24),0)
                        self.assertLessEqual(frames,maximum)
                        self.assertLessEqual(g.wal_bytes(),32+maximum*(size+24))
                        self.assertEqual(g.db.execute('PRAGMA integrity_check').fetchone()[0],'ok')

    def test_writer_lock_rechecks_wal_after_outside_preflight(self):
        g=self.store.gaps;g.db.execute('PRAGMA wal_checkpoint(TRUNCATE)')
        reader=self.reader();other=sqlite3.connect(self.path)
        try:
            reserve=g.reserve_write
            def intervene():
                reserve()
                with other:other.execute('INSERT OR REPLACE INTO meta VALUES(?,?)',('race-fixture',json.dumps('x'*131072)))
            with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536),patch.object(g,'reserve_write',side_effect=intervene):
                with self.assertRaises(index.SessionIndexWalBudgetExceeded):
                    with g.write_transaction():g.put_meta('must-not-publish','wrong')
            self.assertIsNone(g.meta('must-not-publish'))
            self.assertFalse(g.db.in_transaction)
        finally:other.close();reader.close()

    def test_budget_refused_constructor_retains_history_and_closes_its_handle(self):
        g=self.store.gaps;reader=self.reader()
        try:
            binding=g.meta('binding');wal=g.wal_bytes();self.assertGreater(wal,0)
            with patch.object(index,'MAX_WAL_WRITE_START_BYTES',1):
                with self.assertRaises(index.SessionIndexWalBudgetExceeded):
                    index.GapIndex(self.path,'c'*32,self.config,'d'*64)
            self.assertEqual(g.meta('binding'),binding);self.assertLessEqual(g.wal_bytes(),wal)
        finally:reader.close()
        self.store.close()
        moved=self.path.with_suffix('.closed');self.path.replace(moved);moved.replace(self.path)
        self.store=session.SessionStore.load(self.folder)
        self.assertEqual(self.store.gaps.meta('binding'),binding)

if __name__=='__main__':unittest.main()
