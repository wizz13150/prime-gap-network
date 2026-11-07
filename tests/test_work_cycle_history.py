"""Cold cycle duties survive bounded hot eviction; no authority or GPU invented."""
from pathlib import Path
import copy,json,sqlite3,sys,tempfile,unittest,subprocess,os
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_work_cycle import WorkCycleJournal
from test_work_cycle import work,task
import test_running_audit_replay as replay


def finish(j,number):
    w=work(number);j.prepare(w)
    for i in (0,1):
        t=task(10000+number*2+i,i);j.attach(w['request_id'],i,t)
        j.published(w['request_id'],i,t['request'],i,0,f'{20000+number*2+i:064x}')
    j.advance(w['request_id'],'running');j.advance(w['request_id'],'completed')
    return w['request_id']


class ColdHistoryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.j=WorkCycleJournal(self.root,'a'*64)
        self.addCleanup(lambda:self.j.close())
    def restart(self):
        self.j.close();self.j=WorkCycleJournal(self.root,'a'*64)
    def evict(self):
        for n in range(1,6):finish(self.j,n)
        self.assertEqual(len(self.j.value['cycles']),4)
        self.assertNotIn(work(1)['request_id'],[c['work']['request_id']for c in self.j.value['cycles']])
        return work(1)['request_id']
    def test_evicted_cycle_and_exact_two_audit_bindings_survive_cold_restart(self):
        r=self.evict();before=self.j.snapshot(r);self.restart()
        self.assertTrue(self.j.contains(r));self.assertEqual(self.j.snapshot(r),before)
        for i in (0,1):
            t=before['audits'][i];self.assertEqual(self.j.bound_audit(t['request'],t['slot'],t['attempt'],i),r)
            self.assertIsNone(self.j.bound_audit(t['request'],t['slot'],t['attempt']+1,i))
        self.assertFalse(self.j.may_search(r));self.assertFalse(self.j.may_audit(r,0))
    def test_archived_cycle_cannot_be_reprepared_or_retagged(self):
        r=self.evict();hot=self.j.file.read_bytes();old=self.j.snapshot(r)
        self.assertFalse(self.j.prepare(work(1)))
        with self.assertRaisesRegex(ValueError,'changed'):self.j.prepare(dict(work(1),start='160000000000000000000'))
        with self.assertRaisesRegex(ValueError,'retag'):self.j.attach(r,0,task(999))
        self.assertEqual(self.j.snapshot(r),old);self.assertEqual(self.j.file.read_bytes(),hot)
    def test_historical_audit_cannot_pay_for_another_cycle(self):
        r=self.evict();t=self.j.snapshot(r)['audits'][0];self.j.prepare(work(6));hot=self.j.file.read_bytes()
        assigned={k:v for k,v in t.items()if k!='block'};assigned['stage']='assigned'
        with self.assertRaisesRegex(ValueError,'historical'):self.j.attach(work(6)['request_id'],0,assigned)
        self.assertEqual(self.j.file.read_bytes(),hot)
    def test_crash_between_archive_commit_and_hot_replace_keeps_hot_authority(self):
        for n in range(1,5):finish(self.j,n)
        hot=self.j.file.read_bytes();old=self.j.snapshot(work(1)['request_id'])
        with patch('horizon_work_cycle.atomic_bytes',side_effect=OSError('crash after archive')):
            with self.assertRaises(OSError):self.j.prepare(work(5))
        self.assertEqual(self.j.file.read_bytes(),hot);self.assertEqual(self.j.snapshot(work(1)['request_id']),old)
        self.restart();self.assertEqual(self.j.snapshot(work(1)['request_id']),old)
        self.j.hold(work(1)['request_id'],'Unrelated inspection')
        self.assertEqual(self.j.reorg_held(),[])
        self.j.hold(work(1)['request_id'],WorkCycleJournal.reorg_hold)
        self.assertEqual(self.j.reorg_held().count(work(1)['request_id']),1)
    def test_archived_replacement_hold_and_cas_survive_restart(self):
        r=self.evict();t=self.j.snapshot(r)['audits'][0];old=t['block'];new='f'*64
        self.assertTrue(self.j.reopen_audit(r,0,t['request'],0,0,old));self.restart()
        prior=self.j.snapshot(r)['audits'];self.assertIn(r,self.j.reorg_held())
        self.assertTrue(self.j.replace_audit_confirmation(r,0,t['request'],0,0,old,new))
        self.assertFalse(self.j.release_reorg_hold(r,prior))
        self.assertTrue(self.j.release_reorg_hold(r,self.j.snapshot(r)['audits']))
        self.restart();self.assertEqual(self.j.snapshot(r)['audits'][0]['block'],new);self.assertNotIn(r,self.j.reorg_held())
    def test_real_process_exit_after_archive_commit_is_recoverable(self):
        for n in range(1,5):finish(self.j,n)
        hot=self.j.file.read_bytes();self.j.close()
        result=subprocess.run([sys.executable,'-B','-I','-X','utf8',__file__,'--crash-after-archive',str(self.root)],timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(result.returncode,37)
        self.j=WorkCycleJournal(self.root,'a'*64)
        self.assertEqual(self.j.file.read_bytes(),hot);self.assertTrue(self.j.contains(work(1)['request_id']))
        self.j.prepare(work(5));self.restart();self.assertTrue(self.j.contains(work(1)['request_id']))
    def test_real_process_exit_inside_archive_transaction_rolls_back(self):
        r=self.evict();before=self.j.snapshot(r);self.j.close()
        result=subprocess.run([sys.executable,'-B','-I','-X','utf8',__file__,'--crash-in-transaction',str(self.root)],timeout=10,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        self.assertEqual(result.returncode,38)
        self.j=WorkCycleJournal(self.root,'a'*64);self.assertEqual(self.j.snapshot(r),before)
        with self.j.history.database() as db:self.assertEqual(db.execute('PRAGMA integrity_check').fetchone()[0],'ok')
    def test_archive_transaction_failure_leaves_old_duty_untouched(self):
        r=self.evict();before=self.j.snapshot(r)
        # Abort after the payload UPDATE, when refs would be replaced. SQLite
        # must roll back the whole transaction, including its earlier write.
        with self.j.history.database() as db:
            db.execute("CREATE TRIGGER reject_refs BEFORE DELETE ON audit_refs BEGIN SELECT RAISE(ABORT,'injected write failure'); END")
        with self.assertRaises(sqlite3.IntegrityError):self.j.hold(r,WorkCycleJournal.reorg_hold)
        self.assertEqual(self.j.snapshot(r),before);self.restart();self.assertEqual(self.j.snapshot(r),before)
    def test_real_sqlite_full_preserves_hot_and_all_prior_cold_cycles(self):
        self.j.history.maximum_pages=8;known=[];hit=False
        for n in range(1,60):
            hot=self.j.file.read_bytes()if self.j.file.exists()else None
            try:known.append(finish(self.j,n))
            except sqlite3.OperationalError as e:
                self.assertIn('full',str(e));hit=True
                self.assertEqual(self.j.file.read_bytes(),hot)
                break
        self.assertTrue(hit)
        for r in known:self.assertTrue(self.j.contains(r))
        self.assertLessEqual(self.j.history.file.stat().st_size,8*4096)
        self.restart()
        for r in known:self.assertTrue(self.j.contains(r))
    def test_fair_cold_hold_rotation_and_unrelated_holds_are_distinct(self):
        cold=[]
        for n in range(1,42):finish(self.j,n)
        for n in range(1,38):
            r=work(n)['request_id'];self.j.hold(r,WorkCycleJournal.reorg_hold);cold.append(r)
        self.j.hold(work(38)['request_id'],'Unrelated inspection')
        seen=set()
        for _ in range(3):
            page=self.j.reorg_held();self.assertLessEqual(len(page),16);seen.update(page)
            self.restart()
        self.assertEqual(seen,set(cold));self.assertNotIn(work(38)['request_id'],seen)
    def test_wrong_chain_corrupt_identity_and_payload_fail_closed(self):
        r=self.evict();self.j.close()
        with self.assertRaisesRegex(ValueError,'another chain'):WorkCycleJournal(self.root,'b'*64)
        self.j=WorkCycleJournal(self.root,'a'*64)
        with self.j.history.database() as db:db.execute('UPDATE cycles SET payload=? WHERE request=?',(b'{"work":{}}',r))
        with self.assertRaises(ValueError):self.j.snapshot(r)
    def test_history_contains_only_public_metadata_and_uses_indexes(self):
        self.evict()
        with self.j.history.database() as db:
            raw=db.execute('SELECT payload FROM cycles LIMIT 1').fetchone()[0]
            self.assertLess(len(raw),32768);self.assertEqual(set(json.loads(raw)),{'work','stage','audits','hold'})
            plan=db.execute('EXPLAIN QUERY PLAN SELECT cycle FROM audit_refs WHERE request=? AND slot=? AND attempt=?',('1'*64,0,0)).fetchall()
            self.assertTrue(any('PRIMARY KEY' in row[-1]for row in plan))


class ActualHistoricalReplayTests(unittest.TestCase):
    def test_cold_archived_orphan_replays_exact_answer_without_compute(self):
        t=replay.RunningReplayTests();t.setUp();self.addCleanup(t.doCleanups);t.complete();f=t.f
        # Force the actual original cycle out of the hot window using unrelated
        # completed bootstrap cycles. No additional physical auditor is claimed.
        for n in range(11,15):
            w=work(n);w['genesis']='9'*64;f.cycles.prepare(w)
            for i in (0,1):f.cycles.attach(w['request_id'],i,dict(stage='unavailable',anchor='8'*64,reason='no_eligible_independent_work'))
            f.cycles.advance(w['request_id'],'running');f.cycles.advance(w['request_id'],'completed')
        self.assertNotIn(f.work,[c['work']['request_id']for c in f.cycles.value['cycles']])
        f.current.unlink();f.cycles.close();f.cycles=WorkCycleJournal(f.root/'cycles','9'*64);f.flow.cycles=f.cycles
        t.second_canonical=False
        self.assertEqual(f.audits.process_one(),'committed');self.assertFalse(f.flow.report_ready(t.entry))
        f.cycles.close();f.cycles=WorkCycleJournal(f.root/'cycles','9'*64);f.flow.cycles=f.cycles
        audits,value=t.second_lane();self.assertEqual(audits.process_one(),'committed')
        self.assertTrue(f.flow.report_ready(t.entry));self.assertEqual(f.cycles.snapshot(f.work)['stage'],'completed')
        self.assertEqual((f.compute,f.publications,t.second_publications),(0,1,1))


if __name__=='__main__':
    if len(sys.argv)==3 and sys.argv[1]=='--crash-after-archive':
        with WorkCycleJournal(sys.argv[2],'a'*64) as j:
            with patch('horizon_work_cycle.atomic_bytes',side_effect=lambda *args:os._exit(37)):j.prepare(work(5))
        raise AssertionError('Crash boundary was not reached')
    elif len(sys.argv)==3 and sys.argv[1]=='--crash-in-transaction':
        with WorkCycleJournal(sys.argv[2],'a'*64) as j:
            with j.history.database() as db:
                db.execute('UPDATE cycles SET payload=?',(b'{"corrupted":true}',))
                os._exit(38)
        raise AssertionError('Crash boundary was not reached')
    else:unittest.main()
