"""Event schedules and genuine controller callback; no timers, device or RPC."""
import json,sys,tempfile,time,unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
from horizon_progress_cadence import ProgressCadence
from horizon_network_worker import NetworkWorker

def event(n=0,phase='SEARCHING'):
    return dict(phase=phase,positions=str(n),tests=str(3*n),statistics={'larger_gaps':n//10})

class CadenceTests(unittest.TestCase):
    def test_changed_100Hz_source_is_bounded_to_10Hz_exact_inputs(self):
        p=ProgressCadence();seen=[]
        for i in range(100):
            if p.admit(event(i),'ticket-a',10_000_000_000+i*10_000_000):seen.append(i)
        self.assertEqual(seen,list(range(0,100,10)))
        self.assertTrue(p.admit(event(100,'COMPUTED'),'ticket-a',11_000_000_000))
    def test_unchanged_keeps_fresh_heartbeat_without_DB_reads_at10Hz(self):
        p=ProgressCadence();seen=[i for i in range(301)if p.admit(event(7),'ticket',i*10_000_000)]
        self.assertEqual(seen,[0,100,200,300])
    def test_phase_ticket_role_and_same_position_result_changes(self):
        p=ProgressCadence();self.assertTrue(p.admit(event(),'search-a',0))
        self.assertTrue(p.admit(event(),'search-b',1))
        self.assertTrue(p.admit(event(),'audit-b-partition1',2))
        self.assertTrue(p.admit(event(0,'PAUSED_ACK'),'audit-b-partition1',3))
        self.assertTrue(p.admit(event(0,'SEARCHING'),'audit-b-partition1',4))
        changed=event();changed['statistics']['larger_gaps']=1
        self.assertTrue(p.admit(changed,'audit-b-partition1',100_000_004))
    def test_measured_cost_backs_off_to_original2Hz_cap(self):
        for cost,spacing in[(4_000_000,200_000_000),(20_000_000,500_000_000)]:
            p=ProgressCadence();p.observe_cost(cost)
            seen=[i for i in range(101)if p.admit(event(i),'t',i*10_000_000)]
            self.assertEqual(seen,list(range(0,101,spacing//10_000_000)))
    def test_invalid_cache_metadata_uses_durable_reader_not_callback_values(self):
        p=ProgressCadence();bad=event();bad['statistics']={'invalid':float('nan')}
        self.assertTrue(p.admit(bad,'t',0));self.assertFalse(p.admit(bad,'t',1))
    def test_actual_controller_suppression_and_ACK_do_not_query_devices(self):
        w=NetworkWorker.__new__(NetworkWorker);w.audits=SimpleNamespace(presentation=None)
        w.current={'ticket':'a'*64,'request_id':'b'*64};w.sequence=4;w.root=Path('unused-fixture');w.network_notice=None
        frames=[];w.emit=frames.append
        now=[0]
        durable=dict(schema='PGN_HORIZON_MINING_VIEW_V1',positions='5000',tests='6000',phase='Searching')
        with patch('horizon_network_worker.ticket_frame',return_value=durable.copy())as reader,patch('horizon_network_worker.time.monotonic_ns',side_effect=lambda:now[0]):
            w.progress(event(999999));self.assertEqual(reader.call_count,1)
            self.assertEqual(frames[-1]['positions'],'5000')
            now[0]=50_000_000;w.progress(event(1000000));self.assertEqual(reader.call_count,1)
            now[0]=100_000_000;w.progress(event(1000001));self.assertEqual(reader.call_count,2)
            now[0]+=1;w.progress(event(1000001,'PAUSED_ACK'));self.assertEqual(frames[-1]['phase'],'Paused')
            w.current={'ticket':'c'*64,'request_id':'d'*64};now[0]+=1;w.progress(event(1))
            self.assertEqual(frames[-1]['request_id'],'d'*64);self.assertEqual(reader.call_count,4)

if __name__=='__main__':unittest.main()
