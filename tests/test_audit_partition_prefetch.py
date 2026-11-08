"""Close the real queue's producer handoff gap without launching a device."""
from pathlib import Path
from types import SimpleNamespace
import copy,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_mining_flow import AuditGpuProxy
from horizon_gpu_queue import ResidentGpuQueue

class AuditPrefetchTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.worker=SimpleNamespace(root=Path(self.temp.name),source=SimpleNamespace(),args=SimpleNamespace(piece_limit=64))
        self.queue=ResidentGpuQueue(SimpleNamespace(),threading.Event(),threading.Event(),None,None,None,None)
        self.flow=SimpleNamespace(worker=self.worker,gpu=self.queue,bind_audit_cycle=lambda *a:None)
        self.proxy=AuditGpuProxy(self.flow,0);self.audit=dict(request_id='a'*64,slot=0,attempt=0,ranges=[dict(index=i,start=str(100+i*10),end=str(110+i*10))for i in range(5)])
        self.proxy.audit=SimpleNamespace(current_audit=self.audit)
        self.tickets=[dict(ticket_id=f'{i+1:064x}',start=p['start'],end=p['end'])for i,p in enumerate(self.audit['ranges'])]
    def test_all_five_inputs_ready_before_first_result_and_stay_in_one_queue(self):
        self.proxy.register_preparation(self.tickets)
        self.assertEqual(len(self.queue.audits),5)
        self.assertEqual([j.context['audit_position']for j in self.queue.audits],[0]*5)
        self.assertEqual([j.ticket for j in self.queue.audits],self.tickets)
        self.assertEqual([j.context['presentation']['title']for j in self.queue.audits],['Audit 1']*5)
        self.assertEqual([j.context['presentation']['offset']for j in self.queue.audits],['0','10','20','30','40'])
    def test_staged_partitions_keep_audit_service_ready_across_producer_gap(self):
        self.proxy.register_preparation(self.tickets)
        self.queue.update_audit_backlog(100,1,86400)
        self.queue.service.charge('search',1000)
        self.queue.submit(dict(ticket_id='b'*64),dict(role='search'),64)
        for _ in range(5):
            job=self.queue._next();self.assertEqual(job.context['role'],'audit');self.queue.service.charge('audit',10)
        self.assertEqual(self.queue._next().context['role'],'search')
    def test_completed_future_reused_but_changed_work_not_accepted(self):
        self.proxy.register_preparation(self.tickets);future=self.proxy.queue_ticket(self.tickets[0],64);future.set_result({'state':'COMPLETE'})
        self.assertIs(self.proxy.queue_ticket(copy.deepcopy(self.tickets[0]),64),future);self.assertEqual(len(self.queue.audits),5)
        changed=dict(self.tickets[0],end='999')
        with self.assertRaisesRegex(ValueError,'identity changed'):self.proxy.queue_ticket(changed,64)
        with self.assertRaisesRegex(ValueError,'identity changed'):self.proxy.queue_ticket(self.tickets[0],65)
        self.proxy.acknowledge_preparation(self.tickets[0]['ticket_id']);self.assertEqual(len(self.proxy.prepared),4)

if __name__=='__main__':unittest.main()
