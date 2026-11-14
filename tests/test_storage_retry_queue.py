"""Transient storage retains exact Futures and desired ON/OFF; controlled GPU."""
from pathlib import Path
import sys,errno,sqlite3,threading,time,unittest,copy,tempfile
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_gpu_queue import ResidentGpuQueue,retryable_storage_error
from horizon_work_cycle import WorkCycleJournal

class StorageRetryTests(unittest.TestCase):
    def exercise(self,error,off=False):
        enabled=threading.Event();enabled.set();stop=threading.Event();retried=threading.Event()
        computations=[];callbacks=[];failures=[];starts=[]
        class Engine:
            def run_ticket(self,ticket,maximum):
                computations.append((ticket,maximum));return dict(state='COMPLETE',sha256='a'*64)
        def commit(job,result):
            callbacks.append((job,copy.deepcopy(result)))
            if len(callbacks)==1:
                result['sha256']='mutation before failed handoff'
                raise error
            stop.set()
        def failure(e):
            failures.append(e)
            if off:enabled.clear()
            retried.set()
        queue=ResidentGpuQueue(Engine(),enabled,stop,starts.append,commit,lambda:None,failure)
        ticket=dict(ticket_id='b'*64,start='10',end='20');future=queue.submit(ticket,dict(role='audit'),10)
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            self.assertTrue(retried.wait(2));self.assertIs(failures[0],error)
            self.assertEqual(enabled.is_set(),not off);self.assertFalse(future.done())
            if off:
                time.sleep(.35);self.assertEqual(len(computations),1)
                enabled.set();queue.wake()
            thread.join(3);self.assertFalse(thread.is_alive())
            self.assertEqual(len(callbacks),2);self.assertIs(callbacks[0][0],callbacks[1][0])
            self.assertIs(callbacks[0][0].future,future);self.assertEqual(callbacks[0][1],callbacks[1][1])
            self.assertEqual(computations,[(ticket,10)]);self.assertEqual(len(starts),1)
            self.assertEqual(future.result()['sha256'],'a'*64)
        finally:stop.set();enabled.set();queue.wake();thread.join(2)
    def test_ENOSPC_after_computation_retries_same_durable_handoff(self):self.exercise(OSError(errno.ENOSPC,'full'))
    def test_OFF_during_storage_retry_stays_OFF_until_explicit_ON(self):self.exercise(OSError(errno.ENOSPC,'full'),True)
    def test_SQLITE_BUSY_retries_without_turning_desired_state_OFF(self):
        with tempfile.TemporaryDirectory()as folder:
            a=sqlite3.connect(str(Path(folder)/'busy.sqlite'),timeout=0)
            b=sqlite3.connect(str(Path(folder)/'busy.sqlite'),timeout=0)
            try:
                a.execute('CREATE TABLE t(x)');a.commit();a.execute('BEGIN EXCLUSIVE')
                try:b.execute('INSERT INTO t VALUES(1)')
                except sqlite3.OperationalError as error:self.exercise(error)
                else:self.fail('Native SQLite BUSY not raised')
            finally:a.close();b.close()
    def test_native_SQLITE_LOCKED_and_FULL_are_retryable(self):
        a=sqlite3.connect('file:storage-retry-test?mode=memory&cache=shared',uri=True,timeout=0)
        b=sqlite3.connect('file:storage-retry-test?mode=memory&cache=shared',uri=True,timeout=0)
        try:
            a.execute('CREATE TABLE t(x)');a.commit();a.execute('BEGIN');a.execute('SELECT * FROM t').fetchall()
            with self.assertRaises(sqlite3.OperationalError)as locked:b.execute('INSERT INTO t VALUES(1)')
            self.assertTrue(retryable_storage_error(locked.exception))
        finally:a.close();b.close()
        a=sqlite3.connect(':memory:')
        try:
            a.execute('PRAGMA max_page_count=1')
            with self.assertRaises(sqlite3.OperationalError)as full:a.execute('CREATE TABLE t(x)')
            self.assertTrue(retryable_storage_error(full.exception))
        finally:a.close()
    def test_only_storage_codes_are_retryable(self):
        self.assertFalse(retryable_storage_error(PermissionError(errno.EACCES,'denied')))
        self.assertTrue(retryable_storage_error(sqlite3.OperationalError('database is locked')))
        self.assertFalse(retryable_storage_error(sqlite3.OperationalError('no such table: database is locked')))
        self.assertFalse(retryable_storage_error(sqlite3.DatabaseError('database is locked')))
        self.assertFalse(retryable_storage_error(ValueError('invalid scientific report')))
        for code in (5,6,13):
            e=sqlite3.OperationalError('storage');e.sqlite_errorcode=code|(3<<8)
            self.assertTrue(retryable_storage_error(e))

    def test_storage_failure_after_cycle_completed_does_not_reactivate_it(self):
        with tempfile.TemporaryDirectory()as folder:
            work=dict(genesis='a'*64,request_id='b'*64,ticket='c'*64,ticket_number='1',
                      start='150000000000000000000',length='200000000000000',target_numerator='32',target_denominator='1')
            cycles=WorkCycleJournal(Path(folder),work['genesis']);self.addCleanup(cycles.close);cycles.prepare(work)
            for position in (0,1):
                request=str(position+1)*64
                cycles.attach(work['request_id'],position,dict(stage='assigned',request=request,slot=position,attempt=0))
                cycles.published(work['request_id'],position,request,position,0,'d'*64)
            enabled=threading.Event();enabled.set();stop=threading.Event();calls=[];starts=[];handoffs=[];failures=[]
            class Engine:
                def run_ticket(self,ticket,maximum):calls.append(ticket);return dict(state='COMPLETE',sha256='e'*64)
            def activate(job):
                starts.append(job);cycles.advance(work['request_id'],'running')
            def handoff(job,result):
                cycles.advance(work['request_id'],'completed');handoffs.append(result)
                if len(handoffs)==1:raise OSError(errno.ENOSPC,'timing write after durable cycle completion')
                stop.set()
            queue=ResidentGpuQueue(Engine(),enabled,stop,activate,handoff,lambda:None,failures.append)
            future=queue.submit(dict(ticket_id=work['ticket'],start=work['start'],end=str(int(work['start'])+int(work['length']))),dict(role='search'),10)
            thread=threading.Thread(target=queue.run);thread.start()
            try:
                thread.join(3);self.assertFalse(thread.is_alive());self.assertTrue(enabled.is_set())
                self.assertEqual(cycles.snapshot(work['request_id'])['stage'],'completed')
                self.assertEqual(len(starts),1);self.assertEqual(len(calls),1);self.assertEqual(len(handoffs),2)
                self.assertEqual(len(failures),1);self.assertEqual(future.result()['sha256'],'e'*64)
            finally:stop.set();enabled.set();queue.wake();thread.join(2);cycles.close()

if __name__=='__main__':unittest.main()
