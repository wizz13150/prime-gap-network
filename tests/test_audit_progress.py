"""Whole-audit presentation from disjoint real job samples; no device required."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import sys, unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'src/engine/runtime_host'), str(ROOT/'src/protocol')]
from horizon_audit_progress import presentation, whole_audit_frame
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


if __name__=='__main__':unittest.main()
