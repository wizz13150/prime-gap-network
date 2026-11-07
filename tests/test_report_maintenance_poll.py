"""Controlled monotonic clock; run the actual publisher without devices."""
from pathlib import Path
from types import SimpleNamespace
import math
import sys
import tempfile
import unittest
from unittest.mock import patch
P = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(P/'src/engine/runtime_host'), str(P/'src/protocol')]
from horizon_report_poll import ReportPoller, owner_poll_delay
from horizon_mining_flow import MiningFlow
from horizon_work_cycle import WorkCycleJournal


class Clock:
    def __init__(self):self.value=0.0
    def __call__(self):return self.value


class Lane:
    def __init__(self, clock):
        self.clock=clock;self.owner_poll_more=True;self.owner_calls=[];self.audit_calls=[]
        self.fail_owner=self.fail_audit=False;self.owner_cost=0.0
    def reveal_owner_reports(self):
        self.owner_calls.append(self.clock())
        self.clock.value+=self.owner_cost
        if self.fail_owner:raise RuntimeError('owner unavailable')
    def reveal_committed(self):
        self.audit_calls.append(self.clock())
        if self.fail_audit:raise RuntimeError('auditor unavailable')


class MaintenancePollTests(unittest.TestCase):
    def test_fast_owner_sweep_does_not_increase_auditor_frequency(self):
        clock=Clock();owner=Lane(clock);lane=Lane(clock)
        poller=ReportPoller(owner,[lane],clock=clock)
        for second in range(12):
            clock.value=float(second);poller.poll()
        self.assertEqual(owner.owner_calls,list(map(float,range(12))))
        self.assertEqual(owner.audit_calls,[0.0,5.0,10.0])
        self.assertEqual(lane.audit_calls,owner.audit_calls)

    def test_finished_sweep_returns_to_five_second_wake(self):
        clock=Clock();owner=Lane(clock);owner.owner_poll_more=False
        poller=ReportPoller(owner,clock=clock)
        for second in range(12):clock.value=float(second);poller.poll()
        self.assertEqual(owner.owner_calls,[0.0,5.0,10.0])

    def test_expensive_owner_steps_have_bounded_callback_duty(self):
        clock=Clock();owner=Lane(clock);owner.owner_cost=2.0
        poller=ReportPoller(owner,clock=clock);poller.poll()
        self.assertEqual(poller.owner_next,8.0)
        clock.value=7.99;poller.poll();self.assertEqual(len(owner.owner_calls),1)
        clock.value=8.0;poller.poll();self.assertEqual(owner.owner_calls,[0.0,8.0])

    def test_owner_failure_never_starves_audit_lanes_and_notice_stays_stable(self):
        clock=Clock();owner=Lane(clock);owner.fail_owner=True;lane=Lane(clock)
        poller=ReportPoller(owner,[lane],clock=clock)
        self.assertEqual(poller.poll(),'owner unavailable')
        clock.value=1;self.assertEqual(poller.poll(),'owner unavailable')
        clock.value=5;poller.poll()
        self.assertEqual(owner.audit_calls,[0.0,5])
        self.assertEqual(lane.audit_calls,[0.0,5])
        owner.fail_owner=False;clock.value=10;self.assertIsNone(poller.poll())

    def test_one_auditor_failure_does_not_starve_other_lanes(self):
        clock=Clock();owner=Lane(clock);broken=Lane(clock);broken.fail_audit=True;healthy=Lane(clock)
        poller=ReportPoller(owner,[broken,healthy],clock=clock)
        self.assertEqual(poller.poll(),'auditor unavailable')
        self.assertEqual(healthy.audit_calls,[0.0])
        self.assertEqual(owner.audit_calls,[0.0])

    def test_invalid_duration_is_not_an_infinite_sleep(self):
        for value in (-1,math.nan,math.inf,True):
            with self.assertRaises(ValueError):owner_poll_delay(value,True)
        self.assertEqual(owner_poll_delay(.1,True),1.0)
        self.assertEqual(owner_poll_delay(2.0,True),6.0)
        self.assertEqual(owner_poll_delay(2.0,False),6.0)

    def test_legacy_owner_adapter_keeps_default_cadence(self):
        clock=Clock();calls=[]
        owner=SimpleNamespace(reveal_owner_reports=lambda:calls.append(clock()),reveal_committed=lambda:None)
        poller=ReportPoller(owner,clock=clock)
        poller.poll();clock.value=1;poller.poll();clock.value=5;poller.poll()
        self.assertEqual(calls,[0.0,5])

    def publisher(self, continuously_busy=False):
        clock=Clock();owner=Lane(clock);flow=object.__new__(MiningFlow)
        class Stop:
            def is_set(self):return clock.value>=12.0
            def wait(self,seconds):clock.value+=seconds
        class Changed:
            def wait(self,seconds):clock.value+=seconds
            def clear(self):pass
        class Lock:
            def __init__(self):self.entries=0
            def __enter__(self):self.entries+=1
            def __exit__(self,*args):pass
        acknowledgements=[]
        class Outbox:
            def first(self):
                return dict(work=dict(request_id='a'*64),report_root='b'*64) if continuously_busy else None
            def acknowledge(self,*args):acknowledgements.append(args)
        def submit(*args,**kwargs):clock.value+=.25
        lock=Lock()
        flow.worker=SimpleNamespace(quitting=Stop(),audits=owner,network_lock=lock,network_notice=None,submit=submit)
        flow.audit_lanes=[];flow.outbox=Outbox();flow.changed=Changed()
        # Use a real empty cycle journal: these preserved outbox reports are
        # legacy work, while confirmation repair is still part of the publisher.
        with tempfile.TemporaryDirectory() as folder:
            flow.cycles=WorkCycleJournal(folder,'7'*64)
            try:
                with patch('horizon_report_poll.time.monotonic',clock):
                    flow.publish_reports()
            finally:
                flow.cycles.close()
        return owner,lock.entries,acknowledgements,flow.worker.network_notice

    def test_actual_idle_publisher_only_locks_when_maintenance_is_due(self):
        owner,locks,acks,notice=self.publisher()
        self.assertEqual(len(owner.audit_calls),3)
        self.assertGreaterEqual(len(owner.owner_calls),10)
        self.assertLess(locks,15)
        self.assertEqual(acks,[]);self.assertIsNone(notice)

    def test_actual_busy_outbox_does_not_starve_maintenance(self):
        owner,_,acks,notice=self.publisher(True)
        self.assertGreater(len(acks),30)
        self.assertGreaterEqual(len(owner.owner_calls),10)
        self.assertEqual(len(owner.audit_calls),3)
        self.assertIsNone(notice)


if __name__ == '__main__':
    unittest.main()
