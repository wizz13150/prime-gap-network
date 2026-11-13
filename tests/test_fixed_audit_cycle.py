from pathlib import Path
from types import SimpleNamespace
import sys,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/protocol'),str(ROOT/'src/engine/runtime_host')]
from horizon_mining_flow import MiningFlow
from horizon_gpu_queue import ResidentGpuQueue
from horizon_work_cycle import WorkCycleJournal

def audit(n,done=0):
    return dict(request_id=f'{n:064x}',slot=0,attempt=0,counts=[1]*done)
def job(n):
    return SimpleNamespace(context=dict(audit_request_id=f'{n:064x}',audit_slot=0,audit_attempt=0))
def make(credits=(),lanes=()):
    f=object.__new__(MiningFlow)
    f.prefetch=SimpleNamespace(native_service_status=dict(enforced=True,may_request=False,credits=list(credits)))
    f.audit_lanes=[SimpleNamespace(current_audit=x)for x in lanes]
    f.unbound_service_audits=set();f.unbound_service_seeded=False
    return f

class FixedAuditCycleTests(unittest.TestCase):
    def test_two_startup_deliveries_cannot_become_unlimited_drain(self):
        f=make(lanes=[audit(100),audit(101)])
        self.assertTrue(f.may_unbound_audit(job(100)));self.assertTrue(f.may_unbound_audit(job(101)))
        f.prefetch.native_service_status['credits']=[audit(100)]
        f.audit_lanes[0].current_audit=audit(102)
        self.assertFalse(f.may_unbound_audit(job(102)))
        self.assertTrue(f.may_unbound_audit(job(101)))
        f.prefetch.native_service_status['credits']=[audit(100),audit(101)]
        f.prefetch.native_service_status['may_request']=True
        self.assertFalse(f.may_unbound_audit(job(103)))

    def test_restart_counts_core_commit_and_keeps_most_advanced_retained_audit(self):
        f=make(credits=[audit(100)],lanes=[audit(101,4),audit(102)])
        self.assertTrue(f.may_unbound_audit(job(101)))
        self.assertFalse(f.may_unbound_audit(job(102)))
        self.assertEqual(len(f.unbound_service_audits),2)

    def test_only_exact_consumed_service_references_release_local_dispatch(self):
        f=make(lanes=[audit(100),audit(101)])
        f.may_unbound_audit(job(100));f.may_unbound_audit(job(101))
        f.prefetch.native_service_status['credits']=[audit(100)]
        f.unbound_service_audits.difference_update([f.service_identity(audit(100))])
        self.assertFalse(f.may_unbound_audit(job(102))) # Core still says100 is available.
        f.prefetch.native_service_status['credits']=[]
        self.assertTrue(f.may_unbound_audit(job(102)));self.assertFalse(f.may_unbound_audit(job(103)))

    def test_missing_permission_never_becomes_automatic_backlog_draining(self):
        f=make();f.prefetch.native_service_status=None
        self.assertFalse(f.may_unbound_audit(job(100)))
        f.prefetch.native_service_status=dict(enforced=True,may_request=True,credits=[])
        self.assertFalse(f.may_unbound_audit(job(100)))

    def test_native_service_already_ready_allows_one_extra_not_two(self):
        cycle=dict(audits=[dict(stage='committed'),dict(stage='committed')])
        a='a'*64;b='b'*64
        self.assertTrue(WorkCycleJournal.extra_audit_available(cycle,a,[]))
        self.assertTrue(WorkCycleJournal.extra_audit_available(cycle,a,[a]))
        self.assertFalse(WorkCycleJournal.extra_audit_available(cycle,b,[a]))

    def test_queue_has_no_backlog_pressure_or_bulk_mode(self):
        q=ResidentGpuQueue(SimpleNamespace(),threading.Event(),threading.Event(),lambda*a:None,lambda*a:None,lambda*a:None,lambda*a:None)
        view=q.audit_service_view()
        self.assertEqual((view['regular_audits'],view['catch_up_audits']),(2,1))
        self.assertFalse(hasattr(q,'audit_pressure'));self.assertFalse(hasattr(q,'update_audit_backlog'))

if __name__=='__main__':unittest.main()
