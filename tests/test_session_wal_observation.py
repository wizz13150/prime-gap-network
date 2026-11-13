"""Actual private SQLite contention; no GPU, network, or production mutation."""
from contextlib import closing
from pathlib import Path
import json,sqlite3,sys,tempfile,threading,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import pgn_index as index
from pgn_session import SessionStore
from pgn_native_contract import fields
from horizon_mining_flow import MiningFlow
OBSERVATIONS=[]


class Cursor:
    def __init__(self,cursor,after):self.cursor,self.after=cursor,after
    def fetchone(self):
        row=self.cursor.fetchone();self.after(row);return row
    def close(self):self.cursor.close()
    def __getattr__(self,key):return getattr(self.cursor,key)


class Connection:
    def __init__(self,connection,after=None,error=None):self.connection,self.after,self.error=connection,after,error
    def execute(self,sql,*args):
        if sql=='PRAGMA wal_checkpoint(TRUNCATE)':
            if self.error is not None:raise self.error
            return Cursor(self.connection.execute(sql,*args),self.after or (lambda row:None))
        return self.connection.execute(sql,*args)
    def __getattr__(self,key):return getattr(self.connection,key)
    def __enter__(self):self.connection.__enter__();return self
    def __exit__(self,*args):return self.connection.__exit__(*args)


class WalObservationTests(unittest.TestCase):
    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory();self.addCleanup(self.temporary.cleanup)
        self.folder=Path(self.temporary.name)/'session';first=1<<66
        self.cfg=dict(start=str(first),end=str(first+10000),bins=1,report_top=10,**fields(first,first+10000))
        self.store=SessionStore.create(self.folder,self.cfg,{'uuid':'NO-GPU-WAL59'})
        self.store.record_gap(first+1,first+1001,'private fixture')
        self.store.accepted_progress(2000,3)
        self.g=self.store.gaps;self.raw=self.g.db
        self.raw.execute('PRAGMA wal_checkpoint(TRUNCATE)');self.raw.execute('PRAGMA wal_autocheckpoint=0')
        self.addCleanup(self.store.close)
        self.expected=dict(self.g.items())
        self.old_timeout=self.raw.execute('PRAGMA busy_timeout').fetchone()[0]

    def grow(self):
        with self.g.write_transaction():
            for i in range(20):self.g.put_meta('private-volume-'+str(i),'x'*8192)
        self.assertGreater(self.g.wal_bytes(),65536)

    def holder(self,kind):
        held,release,closed=threading.Event(),threading.Event(),threading.Event();errors=[]
        def run():
            try:
                with closing(sqlite3.connect(self.g.path,isolation_level=None))as c:
                    if kind=='reader':c.execute('BEGIN');c.execute('SELECT v FROM meta LIMIT 1').fetchone()
                    else:c.execute('BEGIN IMMEDIATE');c.execute("INSERT INTO meta VALUES('uncommitted-other-writer','1')")
                    held.set()
                    if not release.wait(5):raise TimeoutError('private holder was not released')
                    c.rollback()
            except BaseException as error:errors.append(error);held.set()
            finally:closed.set()
        thread=threading.Thread(target=run);thread.start();self.assertTrue(held.wait(2));self.assertFalse(errors)
        def cleanup():
            release.set();thread.join(3);self.assertFalse(thread.is_alive());self.assertFalse(errors)
        self.addCleanup(cleanup)
        return release,closed

    def append(self):self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0)

    def test_candidate_is_isolated_and_hard_limits_durability_unchanged(self):
        self.assertEqual(Path(index.__file__).resolve(),(ROOT/'src/engine/runtime_host/pgn_index.py').resolve())
        self.assertEqual(index.MAX_WAL_WRITE_START_BYTES,32*1024*1024)
        self.assertEqual(index.MAX_SESSION_INDEX_BYTES,128*1024*1024)
        self.assertEqual(index.WAL_CHECKPOINT_ATTEMPTS,2)
        self.assertEqual(index.WAL_CHECKPOINT_BUSY_MS,100)
        self.assertEqual(self.raw.execute('PRAGMA synchronous').fetchone(),(2,))
        self.assertEqual(self.raw.execute('PRAGMA cache_spill').fetchone(),(0,))

    def test_normal_admission_adds_no_sql_or_checkpoint_below_the_limit(self):
        statements=[];self.raw.set_trace_callback(statements.append)
        try:self.g.reserve_write()
        finally:self.raw.set_trace_callback(None)
        self.assertEqual(statements,[]);self.assertIsNone(self.g.last_wal_checkpoint)

    def test_reader_released_after_first_busy_retries_without_new_job_or_gap(self):
        release,closed=self.holder('reader');self.grow();seen=[]
        def after(row):
            seen.append(row)
            if len(seen)==1:release.set();self.assertTrue(closed.wait(2))
        self.g.db=Connection(self.raw,after)
        before=self.store.sequence
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):self.append()
        self.assertEqual(len(seen),2);self.assertEqual(seen[0][0],1);self.assertEqual(seen[1],(0,0,0))
        self.assertEqual(self.store.sequence,before+1);self.assertFalse(self.store._index_recovery_required)
        self.assertEqual(dict(self.g.items()),self.expected)
        observation=self.g.last_wal_checkpoint
        self.assertEqual(len(observation['attempts']),2);self.assertEqual(observation['wal_after_bytes'],0)
        self.assertEqual(observation['holder'],'unknown')
        self.assertEqual(self.raw.execute('PRAGMA busy_timeout').fetchone()[0],self.old_timeout)
        OBSERVATIONS.append(dict(case='transient_reader',observation=observation))

    def test_held_reader_stops_after_two_attempts_without_advancing_journal(self):
        release,closed=self.holder('reader');self.grow();before=self.store.sequence;raw_journal=self.store.journal.read_bytes();wal=self.g.wal_bytes()
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):
            with self.assertRaises(index.SessionIndexWalBudgetExceeded)as caught:self.append()
        error=caught.exception;o=error.observation
        self.assertEqual(error.code,'SESSION_INDEX_WAL_BUDGET_REACHED');self.assertEqual(len(o['attempts']),2)
        self.assertTrue(all(x['checkpoint'][0]==1 for x in o['attempts']))
        self.assertEqual(o['wal_before_bytes'],wal);self.assertEqual(o['wal_after_bytes'],wal)
        self.assertEqual(o['sqlite_version'],sqlite3.sqlite_version);self.assertGreater(o['elapsed_ms'],0)
        self.assertEqual(o['holder'],'unknown');self.assertNotIn('reader',str(error).lower())
        self.assertNotIn('writer',str(error).lower());self.assertLess(len(str(error)),400)
        OBSERVATIONS.append(dict(case='held_reader',observation=o,message=str(error)))
        self.assertEqual(self.store.sequence,before);self.assertEqual(self.store.journal.read_bytes(),raw_journal)
        self.assertFalse(self.store._index_recovery_required);self.assertEqual(dict(self.g.items()),self.expected)
        self.assertEqual(self.raw.execute('PRAGMA busy_timeout').fetchone()[0],self.old_timeout)
        release.set();self.assertTrue(closed.wait(2))
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):self.append()
        self.assertEqual(self.store.sequence,before+1);self.assertEqual(dict(self.g.items()),self.expected)
        self.assertEqual(self.raw.execute('PRAGMA integrity_check').fetchone(),('ok',))

    def test_writer_conflict_has_same_neutral_observation_then_short_retry_succeeds(self):
        self.grow();release,closed=self.holder('writer');seen=[]
        def after(row):
            seen.append(row)
            if len(seen)==1:release.set();self.assertTrue(closed.wait(2))
        self.g.db=Connection(self.raw,after);before=self.store.sequence
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):self.append()
        self.assertEqual(len(seen),2);self.assertEqual(seen[0][0],1);self.assertEqual(seen[1],(0,0,0))
        self.assertEqual(self.g.last_wal_checkpoint['holder'],'unknown')
        self.assertEqual(self.store.sequence,before+1);self.assertIsNone(self.g.meta('uncommitted-other-writer'))
        self.assertEqual(dict(self.g.items()),self.expected)
        OBSERVATIONS.append(dict(case='transient_writer',observation=self.g.last_wal_checkpoint))

    def test_non_contention_operational_error_is_not_hidden_or_retried(self):
        self.grow();error=sqlite3.OperationalError('private disk fixture');error.sqlite_errorcode=sqlite3.SQLITE_FULL;error.sqlite_errorname='SQLITE_FULL'
        self.g.db=Connection(self.raw,error=error);before=self.store.sequence
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):
            with self.assertRaises(sqlite3.OperationalError)as caught:self.append()
        self.assertIs(caught.exception,error);self.assertEqual(self.store.sequence,before)
        self.assertFalse(self.store._index_recovery_required)
        self.assertEqual(self.raw.execute('PRAGMA busy_timeout').fetchone()[0],self.old_timeout)

    def test_extended_busy_code_is_retained_without_false_holder_attribution(self):
        self.grow();error=sqlite3.OperationalError('private extended busy');error.sqlite_errorcode=sqlite3.SQLITE_BUSY_SNAPSHOT;error.sqlite_errorname='SQLITE_BUSY_SNAPSHOT'
        self.g.db=Connection(self.raw,error=error)
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):
            with self.assertRaises(index.SessionIndexWalBudgetExceeded)as caught:self.append()
        o=caught.exception.observation;self.assertEqual(len(o['attempts']),2)
        self.assertEqual(o['attempts'][0]['sqlite_errorcode'],sqlite3.SQLITE_BUSY_SNAPSHOT)
        self.assertEqual(o['attempts'][0]['checkpoint'],None);self.assertEqual(o['holder'],'unknown')
        self.assertIn('SQLITE_BUSY_SNAPSHOT',str(caught.exception))

    def test_writer_admission_race_records_stage_without_attempting_nested_checkpoint(self):
        release,closed=self.holder('reader');reserve=self.g.reserve_write
        with closing(sqlite3.connect(self.g.path))as other:
            def intervene():
                reserve()
                with other:other.execute("INSERT OR REPLACE INTO meta VALUES('outside-race',?)",(json.dumps('x'*131072),))
            with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536),patch.object(self.g,'reserve_write',side_effect=intervene):
                with self.assertRaises(index.SessionIndexWalBudgetExceeded)as caught:
                    with self.g.write_transaction():self.g.put_meta('must-not-publish','wrong')
        o=caught.exception.observation;self.assertEqual(o['stage'],'writer_admission');self.assertEqual(o['attempts'],[])
        self.assertEqual(o['holder'],'unknown');self.assertIsNone(self.g.meta('must-not-publish'));self.assertFalse(self.raw.in_transaction)
        release.set();self.assertTrue(closed.wait(2))

    def test_flow_keeps_storage_wait_distinct_from_fatal_error_and_does_not_clear_ON(self):
        messages=[];frames=[];enabled=threading.Event();enabled.set()
        class Worker:
            running=enabled
            def message(self,text,phase):messages.append((text,phase))
            def emit(self,frame):
                frames.append(frame);messages.append((frame['message'],frame['phase']))
        flow=object.__new__(MiningFlow);flow.worker=Worker()
        flow.failed(index.SessionIndexWalBudgetExceeded('Storage busy; saved work retained'))
        self.assertEqual(messages[-1][1],'Waiting for storage');self.assertTrue(enabled.is_set())
        self.assertEqual(frames[-1]['schema'],'PGN_HORIZON_MINING_VIEW_UNAVAILABLE_V1')
        flow.failed(RuntimeError('unrelated real failure'))
        self.assertEqual(messages[-1][1],'Needs attention');self.assertTrue(enabled.is_set())

    def test_user_caption_stays_simple_while_diagnostics_keep_bounded_checkpoint_details(self):
        release,closed=self.holder('reader');self.grow();frames=[]
        with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):
            with self.assertRaises(index.SessionIndexWalBudgetExceeded)as caught:self.append()
        error=caught.exception;enabled=threading.Event();enabled.set()
        class Worker:
            running=enabled
            def emit(self,frame):frames.append(frame)
        flow=object.__new__(MiningFlow);flow.worker=Worker();flow.failed(error)
        frame=json.loads(json.dumps(frames[-1]))
        self.assertEqual(frame['phase'],'Waiting for storage');self.assertTrue(enabled.is_set())
        self.assertNotIn('SQLite',frame['message']);self.assertNotIn('checkpoint',frame['message'])
        failure=frame['failure'];self.assertEqual(failure['schema'],'PGN_RUNTIME_FAILURE_V1')
        self.assertEqual(failure['step'],'session_index_checkpoint')
        self.assertEqual(len(failure['causes']),3)
        self.assertIn(sqlite3.sqlite_version,failure['causes'][0]['summary'])
        for i,cause in enumerate(failure['causes'][1:]):
            self.assertIn('attempt='+str(i+1),cause['summary'])
            self.assertIn(str(error.observation['attempts'][i]['checkpoint']),cause['summary'])
            self.assertIn('elapsed=',cause['summary']);self.assertLessEqual(len(cause['summary']),400)
        self.assertNotIn(str(self.g.path),json.dumps(frame))
        release.set();self.assertTrue(closed.wait(2))


if __name__=='__main__':unittest.main()
