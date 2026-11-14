"""Batch scheduling/crash recovery with an explicit producer test double.

Real process, factor output and native GPU contracts are qualified separately.
This test never labels its controlled prime oracle as arithmetic evidence.
"""
from pathlib import Path
from types import SimpleNamespace
import hashlib
import json
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ['src/engine/runtime_host','src/engine/shared_composite','src/protocol']]
import horizon_preparation_resolver as resolver
from horizon_preparation_scopes import plan_preparation, LOW
from pgn_native_contract import fields
from horizon_engine_host import ticket_configuration


class ProviderFixture:
    def __init__(self):
        self.plans={};self.jobs={};self.job_ids={};self.requests=[];self.executed=[];self.retired=[]
        self.queue=SimpleNamespace(entries=lambda:[dict(id=k,state=v)for k,v in self.jobs.items()],acknowledge=self.acknowledge)
    def request(self, requests):
        self.requests.append(requests)
        plan=plan_preparation(requests,lambda _:True)
        identity=hashlib.sha256(resolver.canonical(plan.document())).hexdigest()
        self.plans[identity]=plan;job=hashlib.sha256(resolver.canonical(plan.document()['scopes'])).hexdigest()
        self.job_ids[identity]=job;self.jobs.setdefault(job,'queued')
        return identity,plan
    def _input(self, identity):return self.plans[identity],self.job_ids[identity],{}
    def pump(self, **resources):
        if resources['cancelled']():raise InterruptedError('Controlled cancellation')
        identity=next((k for k,v in self.jobs.items()if v=='queued'),None)
        if identity is not None:self.jobs[identity]='ready';self.executed.append(identity)
        return identity
    def prepared(self, identity, **_):
        if self.jobs[self.job_ids[identity]]!='ready':raise AssertionError('Unprepared source returned')
        return self.plans[identity]
    def acknowledge(self, job):
        if job not in self.jobs:return
        if self.jobs[job]!='ready':raise AssertionError('Unprepared source released')
        del self.jobs[job]
    def retire(self, plan):self.retired.append(plan)


class PreparationResolverTests(unittest.TestCase):
    def test_OFF_cancels_preparation_but_ON_resumes_resident_lane(self):
        entered=threading.Event();cancelled=threading.Event();ready=threading.Event()
        enabled=threading.Event();enabled.set();provider=ProviderFixture();original=provider.pump
        def produce(**resources):
            if not entered.is_set():
                entered.set()
                deadline=__import__('time').monotonic()+2
                while not resources['cancelled']():
                    if __import__('time').monotonic()>deadline:raise AssertionError('OFF was not delivered')
                    __import__('time').sleep(.01)
                cancelled.set();raise InterruptedError('controlled OFF')
            result=original(**resources);ready.set();return result
        provider.pump=produce
        r=resolver.PreparationResolver(self.root/'controlled',provider,logical_cpu=0,memory_mib=128,timeout=1,prefetch=True)
        self.addCleanup(r.close);r.bind_run_control(enabled);r.register([self.ticket(1)])
        self.assertTrue(entered.wait(2));enabled.clear();self.assertTrue(cancelled.wait(2))
        self.assertTrue(r._thread.is_alive());self.assertFalse(ready.is_set())
        enabled.set();self.assertTrue(ready.wait(3));self.assertTrue(r._thread.is_alive())

    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.provider=ProviderFixture()
        self.r=resolver.PreparationResolver(self.root,self.provider,logical_cpu=0,memory_mib=128,timeout=1)
        self.a=(LOW//30+100)*30
    def ticket(self, i):
        a=self.a+i*10**13;b=a+2*10**12
        return dict(schema='PGN_HORIZON_ENGINE_TICKET_V1',ticket_id=('%064x'%i),start=str(a),end=str(b),
                    target_merit_q16=29*65536,threshold_policy=fields(a,b)['threshold_policy'])
    def test_resident_prefetch_prepares_five_scopes_before_any_finalizer_call(self):
        ready=threading.Event();pump=self.provider.pump
        def produce(**resources):
            value=pump(**resources)
            if value is not None:ready.set()
            return value
        self.provider.pump=produce
        r=resolver.PreparationResolver(self.root,self.provider,logical_cpu=0,memory_mib=128,timeout=1,prefetch=True)
        self.addCleanup(r.close);tickets=[self.ticket(i)for i in range(1,6)];r.register(tickets)
        self.assertTrue(ready.wait(2));self.assertEqual(len(self.provider.executed),1)
        self.assertEqual(len(r(ticket_configuration(tickets[0])).selected),5)
        self.assertEqual(len(self.provider.executed),1)
        self.assertTrue(r.close());self.assertFalse(r._thread.is_alive())

    def test_registration_remains_available_while_one_owned_preparation_is_running(self):
        entered=threading.Event();release=threading.Event();pump=self.provider.pump
        def produce(**resources):
            entered.set()
            while not release.wait(.01):
                if resources['cancelled']():raise InterruptedError('Controlled producer cancelled')
            return pump(**resources)
        self.provider.pump=produce
        r=resolver.PreparationResolver(self.root,self.provider,logical_cpu=0,memory_mib=128,timeout=1,prefetch=True)
        self.addCleanup(r.close)
        try:
            r.register([self.ticket(1)]);self.assertTrue(entered.wait(2))
            r.register([self.ticket(2)])
            self.assertEqual(len(r._rows()),2)
            self.assertFalse(self.provider.executed)
        finally:release.set();self.assertTrue(r.close())

    def test_close_cancels_prefetch_and_retains_the_issued_work(self):
        entered=threading.Event()
        def produce(**resources):
            entered.set()
            while not resources['cancelled']():threading.Event().wait(.01)
            raise InterruptedError('Controlled producer cancelled')
        self.provider.pump=produce
        r=resolver.PreparationResolver(self.root,self.provider,logical_cpu=0,memory_mib=128,timeout=1,prefetch=True)
        self.addCleanup(r.close);r.register([self.ticket(1)]);self.assertTrue(entered.wait(2))
        self.assertTrue(r.close());self.assertFalse(self.provider.executed);self.assertEqual(len(r._rows()),1)
        with self.assertRaises(InterruptedError):r.register([self.ticket(2)])

    def test_five_registered_audit_partitions_use_one_shared_preparation(self):
        tickets=[self.ticket(i)for i in range(1,6)];self.r.register(tickets)
        self.assertEqual(self.provider.executed,[])
        for ticket in tickets:
            plan=self.r(ticket_configuration(ticket))
            self.assertEqual(len(plan.selected),5)
        self.assertEqual(len(self.provider.executed),1)
        self.assertEqual(len(self.provider.requests),1)
    def test_batch_intent_recovers_same_work_after_enqueue_before_assignment_crash(self):
        tickets=[self.ticket(i)for i in range(1,6)];self.r.register(tickets)
        atomic=resolver.atomic_bytes
        def crash(path,data):
            if path==self.r.file and any(x['plan']is not None for x in json.loads(data)):
                raise RuntimeError('Controlled crash before assignment commit')
            return atomic(path,data)
        with patch.object(resolver,'atomic_bytes',side_effect=crash):
            with self.assertRaises(RuntimeError):self.r(ticket_configuration(tickets[0]))
        self.assertTrue(self.r.intent.exists());self.assertEqual(self.provider.executed,[])
        restarted=resolver.PreparationResolver(self.root,self.provider,logical_cpu=0,timeout=1)
        restarted.register([self.ticket(6)])
        restarted(ticket_configuration(tickets[0]))
        self.assertEqual(len(self.provider.executed),1)
        self.assertEqual(self.provider.requests[0],self.provider.requests[1])
        self.assertIsNone(next(row for row in restarted._rows()if row['work_id']==self.ticket(6)['ticket_id'])['plan'])
        self.assertFalse(restarted.intent.exists())
    def test_changed_or_unknown_issued_work_is_not_silently_rebound(self):
        ticket=self.ticket(1);self.r.register([ticket]);before=self.r.file.read_bytes()
        changed=self.ticket(2);changed['ticket_id']=ticket['ticket_id']
        with self.assertRaises(ValueError):self.r.register([changed])
        self.assertEqual(self.r.file.read_bytes(),before)
        with self.assertRaises(ValueError):self.r(ticket_configuration(self.ticket(2)))
        self.assertFalse(self.provider.executed)
    def test_shutdown_preserves_registered_work_without_starting_cpu(self):
        ticket=self.ticket(1);self.r.register([ticket]);self.r.stopping.set()
        with self.assertRaises(InterruptedError):self.r(ticket_configuration(ticket))
        self.assertFalse(self.provider.executed);self.assertIsNone(self.r._rows()[0]['plan'])
    def test_failed_preparation_is_not_retried_in_a_hidden_loop(self):
        ticket=self.ticket(1);self.r.register([ticket]);self.r._batch()
        identity=self.r._rows()[0]['plan'];self.provider.jobs[self.provider.job_ids[identity]]='failed'
        with self.assertRaises(resolver.PreparationRecoveryRequired):self.r(ticket_configuration(ticket))
        self.assertFalse(self.provider.executed)
    def test_registration_is_bounded_and_idempotent(self):
        tickets=[self.ticket(i)for i in range(64)];self.r.register(tickets)
        before=self.r.file.read_bytes();self.r.register([tickets[0]])
        self.assertEqual(self.r.file.read_bytes(),before)
        with self.assertRaises(BufferError):self.r.register([self.ticket(64)])
        self.assertEqual(self.r.file.read_bytes(),before)

    def test_concurrent_registration_waits_for_short_batch_commit_without_losing_input(self):
        ticket=self.ticket(1);self.r.register([ticket])
        entered=threading.Event();release=threading.Event();errors=[]
        request=self.provider.request
        def slow(work):
            entered.set()
            if not release.wait(3):raise TimeoutError('Controlled batch not released')
            return request(work)
        def batch():
            try:self.r._batch()
            except BaseException as exc:errors.append(exc)
        def register():
            try:self.r.register([self.ticket(2)])
            except BaseException as exc:errors.append(exc)
        with patch.object(self.provider,'request',side_effect=slow):
            first=threading.Thread(target=batch);second=threading.Thread(target=register)
            first.start()
            try:
                self.assertTrue(entered.wait(2));second.start();release.set()
            finally:
                release.set();first.join(3)
                if second.ident is not None:second.join(3)
        self.assertEqual(errors,[])
        self.assertEqual(len(self.r._rows()),2)
        self.assertIsNone(self.r._rows()[1]['plan'])

    def test_durable_handoff_releases_only_the_last_shared_source_consumer(self):
        first=self.ticket(1);self.r.register([first]);self.r(ticket_configuration(first))
        second=dict(first,ticket_id='f'*64)
        self.r.register([second]);self.r(ticket_configuration(second))
        self.assertEqual(len(self.provider.jobs),1)
        self.r.acknowledge(first['ticket_id'])
        self.assertEqual(len(self.provider.jobs),1)
        self.r.acknowledge(second['ticket_id']);self.r.acknowledge(second['ticket_id'])
        self.assertEqual(self.provider.jobs,{})
        self.assertEqual(self.r._rows(),[])

    def test_handoff_intent_recovers_after_crash_before_reference_release(self):
        ticket=self.ticket(1);self.r.register([ticket]);self.r(ticket_configuration(ticket))
        atomic=resolver.atomic_bytes
        def crash(path,data):
            if path==self.r.file:raise RuntimeError('Controlled handoff crash')
            return atomic(path,data)
        with patch.object(resolver,'atomic_bytes',side_effect=crash):
            with self.assertRaises(RuntimeError):self.r.acknowledge(ticket['ticket_id'])
        self.assertTrue(self.r.ack_intent.exists())
        self.r.register([self.ticket(2)])
        self.assertFalse(self.r.ack_intent.exists());self.assertFalse(self.provider.jobs)
        self.assertEqual([r['work_id']for r in self.r._rows()],[self.ticket(2)['ticket_id']])


if __name__=='__main__':unittest.main()
