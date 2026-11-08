"""Whole-audit presentation from disjoint real job samples; no device required."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import sys, unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'src/engine/runtime_host'), str(ROOT/'src/protocol')]
from horizon_audit_progress import presentation, whole_audit_frame, WholeAuditPresentation
from horizon_network_worker import NetworkWorker


class AuditProgressTests(unittest.TestCase):
    def setUp(self):
        # Unequal disjoint inputs ensure this is a position-weighted sum,
        # not a count of partitions or a fictitious contiguous span.
        self.audit = dict(request_id='a'*64, slot=0, attempt=0,
            ranges=[dict(index=i, start=str(10**20+i*1000), end=str(10**20+i*1000+n))
                    for i,n in enumerate([100,200,300,400,500])])

    def item(self, i, audit=None):
        audit = audit or self.audit
        return presentation(audit, audit['ranges'][i], Path('/internal')/str(i))

    def sample(self, item, n, phase='Searching'):
        return dict(ticket_id='c'*64, start=item['part_start'], end=item['part_end'],
            current=str(int(item['part_start'])+n), positions=str(n), phase=phase,
            speed_rate=30, eta_rate=30, source_stale=False, source_age_ns='0')

    def test_all_internal_boundaries_keep_one_identity_and_do_not_complete_early(self):
        frames=[]
        for i,n in enumerate([100,200,300,400,500]):
            item=self.item(i)
            frames += [whole_audit_frame(item), whole_audit_frame(item,self.sample(item,n,'Report ready'))]
        self.assertEqual([int(f['positions']) for f in frames], [0,100,100,300,300,600,600,1000,1000,1500])
        self.assertEqual({f['positions_total'] for f in frames},{'1500'})
        self.assertEqual(len({f['ticket_id'] for f in frames}),1)
        self.assertEqual([f['phase'] for f in frames],['Searching']*9+['Report ready'])
        for frame in frames:
            self.assertFalse({'start','end','current'} & frame.keys())
            self.assertEqual(frame['progress_scope'],'audit')
            self.assertNotIn('partition',frame['title'])

    def test_pause_and_resume_keep_exact_complete_audit_quantity(self):
        item=self.item(3);sample=self.sample(item,123)
        paused=whole_audit_frame(item,sample,paused=True)
        self.assertEqual((paused['positions'],paused['positions_total'],paused['phase']),('723','1500','Paused'))
        self.assertIsNone(paused['speed_rate']);self.assertIsNone(paused['eta_rate'])
        resumed=whole_audit_frame(item,sample)
        self.assertEqual(resumed['ticket_id'],paused['ticket_id'])
        self.assertEqual(resumed['positions'],paused['positions'])

    def test_two_lanes_and_new_attempt_do_not_merge_progress(self):
        one=self.item(2)
        two=self.item(0,dict(self.audit,slot=1))
        new=self.item(0,dict(self.audit,attempt=1))
        self.assertEqual(len({i['audit_id'] for i in [one,two,new]}),3)
        self.assertEqual([whole_audit_frame(i)['positions'] for i in [one,two,one]],['300','0','300'])

    def test_missing_first_checkpoint_keeps_prefix_without_inventing_rates(self):
        worker=NetworkWorker.__new__(NetworkWorker);worker.sequence=42
        worker.audits=SimpleNamespace(presentation=self.item(4));frames=[];worker.emit=frames.append
        with patch('horizon_network_worker.ticket_frame',side_effect=FileNotFoundError):
            worker._present_progress(dict(phase='STARTING'))
        frame=frames[-1]
        self.assertEqual((frame['positions'],frame['positions_total'],frame['phase']),('1000','1500','Searching'))
        self.assertTrue(frame['source_stale']);self.assertIsNone(frame['speed_rate'])

    def test_worker_boundary_does_not_publish_an_internal_report_ready(self):
        worker=NetworkWorker.__new__(NetworkWorker);worker.sequence=42;frames=[];worker.emit=frames.append
        for i,n in enumerate([100,200,300,400,500]):
            item=self.item(i);worker.audits=SimpleNamespace(presentation=item)
            with patch('horizon_network_worker.ticket_frame',return_value=self.sample(item,n,'Report ready')):
                worker._present_progress(dict(phase='COMPUTED'))
        self.assertEqual([f['phase'] for f in frames],['Searching']*4+['Report ready'])
        self.assertEqual([f['positions'] for f in frames],['100','300','600','1000','1500'])

    def test_incomplete_index_and_unbound_first_chunk_are_not_controller_faults(self):
        worker=NetworkWorker.__new__(NetworkWorker);worker.sequence=42
        worker.audits=SimpleNamespace(presentation=self.item(2));frames=[];worker.emit=frames.append
        for error in [KeyError('progress'), ValueError('No qualified optimized worker identity for this session')]:
            with patch('horizon_network_worker.ticket_frame',side_effect=error):
                worker._present_progress(dict(phase='STARTING'))
        for frame in frames:
            self.assertEqual(frame['phase'],'Searching')
            self.assertEqual(frame['positions'],'300')
            self.assertTrue(frame['source_stale']);self.assertIsNone(frame['speed_rate'])
            self.assertTrue(frame['progress_notice'])

    def test_other_range_or_out_of_bounds_is_rejected_not_mislabelled(self):
        item=self.item(0)
        for change in [dict(start='1'),dict(end='1'),dict(positions='101'),dict(positions='-1')]:
            with self.assertRaises(ValueError):whole_audit_frame(item,dict(self.sample(item,10),**change))

    def result_sample(self, item, count):
        rows=[dict(gap=100+i,lower_prime=str(int(item['part_start'])+i*2),
                   upper_prime=str(int(item['part_start'])+i*2+100+i),merit=str(22+i)) for i in range(count)]
        return dict(self.sample(item,min(count*10,int(item['part_end'])-int(item['part_start']))),
                    bits=67,minimum_gap=100,target_merit=25,
                    scopes=dict(ticket=dict(statistics=dict(count_total=count,at_or_above_target=0,
                        threshold_counts={'100':count},merit_band_counts={'22':count},statistics_target=25),
                        best=list(reversed(rows)),recent=list(reversed(rows)))))

    def test_results_survive_empty_partition_boundary_and_accumulate_once(self):
        audit=WholeAuditPresentation();one,two=self.item(0),self.item(1)
        first=self.result_sample(one,2);audit.frame(one,first)
        boundary=audit.frame(two)
        self.assertEqual(boundary['scopes']['ticket']['statistics']['count_total'],2)
        self.assertEqual(len(boundary['scopes']['ticket']['best']),2)
        self.assertEqual(boundary['target_merit'],25)
        self.assertIsNone(boundary['eta_rate'])
        audit.frame(two,self.result_sample(two,1))
        for unused in range(4):latest=audit.frame(two,self.result_sample(two,3))
        scope=latest['scopes']['ticket']
        self.assertEqual(scope['statistics']['count_total'],5)
        self.assertEqual(scope['statistics']['threshold_counts'],{'100':5})
        self.assertEqual(len(scope['best']),5)
        self.assertEqual(int(scope['recent'][0]['lower_prime']),int(two['part_start'])+4)
        self.assertNotIn('target_merit',first['scopes']['ticket']['best'][0])

    def test_new_audit_resets_result_views_and_pause_does_not_clear_them(self):
        audit=WholeAuditPresentation();one=self.item(0);audit.frame(one,self.result_sample(one,2))
        paused=audit.frame(self.item(1),paused=True)
        self.assertEqual(len(paused['scopes']['ticket']['recent']),2)
        self.assertEqual(paused['phase'],'Paused')
        other=self.item(0,dict(self.audit,slot=1));fresh=audit.frame(other)
        self.assertNotIn('scopes',fresh)
        self.assertNotIn('target_merit',fresh)

    def test_worker_keeps_result_views_across_actual_progress_callback_boundaries(self):
        worker=NetworkWorker.__new__(NetworkWorker);worker.sequence=42;frames=[];worker.emit=frames.append
        worker.audits=SimpleNamespace(presentation=self.item(0))
        with patch('horizon_network_worker.ticket_frame',return_value=self.result_sample(self.item(0),2)):
            worker._present_progress(dict(phase='COMPUTED'))
        worker.audits.presentation=self.item(1)
        with patch('horizon_network_worker.ticket_frame',side_effect=FileNotFoundError):
            worker._present_progress(dict(phase='STARTING'))
        self.assertEqual(frames[-1]['scopes']['ticket']['statistics']['count_total'],2)
        self.assertEqual(len(frames[-1]['scopes']['ticket']['best']),2)


if __name__=='__main__':unittest.main()
