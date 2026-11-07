"""Real SessionStore/SQLite/recovery, temporary files; no GPU or worker."""
import hashlib,json,math,sys,tempfile,unittest
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from pgn_session import SessionStore,MAX_JOURNAL_EVENT_BYTES
from pgn_native_contract import fields

class EventBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name)/'session';first=1<<66
        config=dict(start=str(first),end=str(first+2),bins=1,report_top=10,**fields(first,first+2))
        self.store=SessionStore.create(self.folder,config,{'uuid':'CPU-QA-no-device'})
        self.addCleanup(lambda:self.store.close())
    def snapshot(self):
        return (self.store.sequence,self.store.gaps.meta('journal_sequence'),self.store.gaps.meta('journal_offset'),
                hashlib.sha256(self.store.journal.read_bytes()).hexdigest(),self.store.gaps.meta('state'))
    def test_invalid_event_does_not_poison_durable_prefix(self):
        before=self.snapshot()
        cases=[{'sequence':99},{'type':'gap'},{'note':'x'*MAX_JOURNAL_EVENT_BYTES},
               {'note':'é'*MAX_JOURNAL_EVENT_BYTES},{'seconds':math.nan},{'seconds':math.inf},{'seconds':-math.inf}]
        for values in cases:
            with self.subTest(values=list(values)):
                with self.assertRaises(ValueError):self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0,**values)
                self.assertEqual(self.snapshot(),before)
        self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0);self.store.close();self.store=SessionStore.load(self.folder)
        self.assertEqual(self.store.sequence,2);self.assertEqual(self.store.meta['state'],'PAUSED')
    def test_exact_limit_survives_actual_journal_replay(self):
        first_offset=self.store.gaps.meta('journal_offset')
        frame={'sequence':2,'type':'state','state':'PAUSED','active_seconds':0,'paused_seconds':0,'note':''}
        overhead=len((json.dumps(frame,separators=(',',':'))+'\n').encode())
        payload='x'*(MAX_JOURNAL_EVENT_BYTES-overhead)
        self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0,note=payload)
        self.assertEqual(self.store.journal.stat().st_size-first_offset,MAX_JOURNAL_EVENT_BYTES)
        committed=self.snapshot()
        with self.assertRaises(ValueError):self.store.append('state',state='PAUSED',active_seconds=0,paused_seconds=0,note=payload+'xx')
        self.assertEqual(self.snapshot(),committed)
        # Model a DB frontier still at the preceding durable event. Recovery has
        # to read the full new frame rather than merely trusting the latest DB.
        with self.store.gaps.db:
            self.store.gaps.put_meta('journal_sequence',1)
            self.store.gaps.put_meta('journal_offset',first_offset)
        self.store.close();self.store=SessionStore.load(self.folder)
        self.assertEqual(self.store.sequence,2);self.assertEqual(self.store.meta['state'],'PAUSED')
        self.assertEqual(self.store.gaps.meta('journal_offset'),self.store.journal.stat().st_size)
        self.assertEqual(self.snapshot(),committed)

if __name__=='__main__':unittest.main()
