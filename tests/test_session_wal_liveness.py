"""SQLite snapshots/recycling and retry of one exact job; no GPU required."""
from contextlib import closing
from pathlib import Path
import json, sqlite3, sys, tempfile, threading, unittest
from unittest.mock import patch
ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'src/engine/runtime_host'), str(ROOT/'src/protocol')]
import pgn_index as index
from pgn_native_contract import fields
from pgn_session import SessionStore
from horizon_gpu_queue import ResidentGpuQueue


class WalLivenessTests(unittest.TestCase):
    def test_presentation_decoder_does_not_pin_the_sqlite_snapshot(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory); first=1<<66
            cfg=dict(start=str(first),end=str(first+10000),bins=1,report_top=10,**fields(first,first+10000))
            store=SessionStore.create(folder,cfg,{'uuid':'NO-GPU'})
            store.accepted_progress(0,0)
            decoding, release=threading.Event(),threading.Event(); errors=[]; snapshots=[]
            original=json.loads
            def decode(value,*args,**kwargs):
                if threading.current_thread().name=='presentation-reader' and not decoding.is_set():
                    decoding.set()
                    if not release.wait(3):raise TimeoutError('test decoder not released')
                return original(value,*args,**kwargs)
            def read():
                try:snapshots.append(index.read_presentation_snapshot(store.gaps.path))
                except BaseException as error:errors.append(error)
            thread=threading.Thread(target=read,name='presentation-reader')
            try:
                with patch.object(index.json,'loads',decode):
                    thread.start();self.assertTrue(decoding.wait(2))
                    with store.gaps.db:store.gaps.put_meta('reader-recycling-proof',1)
                    result=store.gaps.db.execute('PRAGMA wal_checkpoint(TRUNCATE)').fetchone()
                    self.assertEqual(result[0],0,'presentation decoding retained the read transaction')
                    self.assertEqual(store.gaps.wal_bytes(),0)
                    release.set();thread.join(2)
                self.assertFalse(thread.is_alive());self.assertFalse(errors);self.assertEqual(len(snapshots),1)
                self.assertEqual(snapshots[0]['progress'],{'positions':0,'tests':0})
            finally:release.set();thread.join(3);store.close()

    def test_thousands_of_journal_updates_recycle_without_losing_any_gap(self):
        with tempfile.TemporaryDirectory() as directory:
            folder=Path(directory);first=1<<66
            cfg=dict(start=str(first),end=str(first+10**7),bins=100,report_top=100,**fields(first,first+10**7))
            store=SessionStore.create(folder,cfg,{'uuid':'NO-GPU'});resets=0;previous=store.gaps.wal_bytes();expected={}
            try:
                with patch.object(index,'MAX_WAL_WRITE_START_BYTES',65536):
                    for i in range(1200):
                        if i%8==0:
                            low=first+i*2000;high=low+1000;expected[low]=high
                            store.record_gap(low,high,'Native_verified')
                        store.accepted_progress((i+1)*2000,(i+1)*17)
                        # Exercise the same separate read-only connection as UI frames.
                        snapshot=index.read_presentation_snapshot(store.gaps.path)
                        self.assertEqual(snapshot['progress']['positions'],(i+1)*2000)
                        now=store.gaps.wal_bytes();resets+=now<previous;previous=now
                    self.assertGreater(resets,3)
                self.assertEqual(dict(store.gaps.items()),expected)
                sequence=store.sequence;store.close();store=SessionStore.load(folder)
                self.assertEqual(store.sequence,sequence);self.assertEqual(dict(store.gaps.items()),expected)
                self.assertEqual(store.gaps.db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
            finally:store.close()

    def test_pinned_reader_retries_exact_future_without_clearing_ON(self):
        enabled,stopping=threading.Event(),threading.Event();enabled.set();calls=[];done=[];errors=[]
        class Engine:
            def run_ticket(self,ticket,maximum):
                calls.append((ticket.copy(),maximum))
                if len(calls)==1:raise index.SessionIndexWalBudgetExceeded('pinned fixture reader')
                return {'state':'COMPLETE','ticket_id':ticket['ticket_id']}
        def complete(job,result):done.append(job);stopping.set()
        queue=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:None,complete,lambda:None,errors.append)
        future=queue.submit({'ticket_id':'a'*64},{'role':'search'},100)
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            thread.join(3);self.assertFalse(thread.is_alive());self.assertTrue(enabled.is_set())
            self.assertEqual(len(calls),2);self.assertEqual(calls[0],calls[1]);self.assertEqual(len(done),1)
            self.assertIs(done[0].future,future);self.assertEqual(future.result()['state'],'COMPLETE')
            self.assertEqual(len(errors),1)
        finally:stopping.set();enabled.set();queue.wake();thread.join(3)

    def test_OFF_during_storage_wait_stays_OFF_and_preserves_future(self):
        enabled,stopping=threading.Event(),threading.Event();enabled.set();calls=[];blocked=threading.Event()
        class Engine:
            def run_ticket(self,ticket,maximum):
                calls.append(ticket)
                raise index.SessionIndexWalBudgetExceeded('pinned fixture reader')
        def notice(error):enabled.clear();blocked.set()
        queue=ResidentGpuQueue(Engine(),enabled,stopping,lambda job:None,lambda *a:None,lambda:None,notice)
        future=queue.submit({'ticket_id':'a'*64},{'role':'search'},100)
        thread=threading.Thread(target=queue.run);thread.start()
        try:
            self.assertTrue(blocked.wait(2));self.assertFalse(enabled.is_set());self.assertFalse(future.done())
            self.assertEqual(len(calls),1);self.assertEqual(len(queue.search),1)
        finally:stopping.set();queue.wake();thread.join(3)


if __name__=='__main__':unittest.main()
