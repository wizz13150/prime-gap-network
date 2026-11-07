"""Real local journals/index/archive; synthetic known956 geometry, no GPU/chain."""
from pathlib import Path
from contextlib import nullcontext
from dataclasses import replace
from types import SimpleNamespace
import gzip,hashlib,json,sys,tempfile,threading,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ('src/engine/runtime_host','src/engine/shared_composite','src/protocol','tests')]
import test_preparation_provider as provider_fixture
from test_report_gap_repair import indexed_report
from horizon_preparation_provider import PreparedSourcePlan
from horizon_preparation_scopes import PreparationPlan
from horizon_engine_host import EngineService,ticket_configuration
from horizon_network_audits import NetworkAudits,partition_ticket
from horizon_audit_partition_archive import NAME
from pgn_session import SessionStore
import horizon_ticket_archive as archive


class AuditPartitionArchiveTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
        old,cfg,_=provider_fixture.PreparationProviderTests.brian_fixture()
        self.part=dict(index=0,start=cfg['start'],end=cfg['end'])
        self.input=dict(request_id='b'*64,ticket='c'*64,report_root='d'*64,slot=0,attempt=0,
            target_merit_q16=29*65536,ranges=[self.part],counts=[])
        job=partition_ticket(self.input,self.part);cfg=ticket_configuration(job)
        cfg.update(preseal_required=True,complement_feed_sha256=old.feeds.select(int(cfg['start']),int(cfg['end'])).fingerprint)
        self.cfg=cfg;self.folder=self.root/'tickets'/cfg['ticket_id']
        plan=PreparationPlan(tuple(replace(w,work_id=cfg['ticket_id'])for w in old.plan.selected),(),old.plan.scopes)
        prepared=PreparedSourcePlan(plan,old.bundle,old.feeds,old.exact_prime)
        self.raw=indexed_report(cfg,[])
        store=SessionStore.create(self.folder,cfg,dict(uuid='NO-HARDWARE-AUDIT-RETENTION-FIXTURE'))
        try:store.accepted_progress(int(cfg['end'])-int(cfg['start']),0);store.closed('COMPLETE',0,0)
        finally:store.close()
        (self.folder/'report.json').write_bytes(archive.canonical(self.raw))
        self.acked=[];self.offered=[];self.computations=0
        self.service=EngineService.__new__(EngineService);self.service.root=self.root
        self.service.archives=SimpleNamespace(use_ticket=lambda _:nullcontext(),offer=self.offered.append)
        def acknowledge(ticket):
            self.assertTrue((self.folder/NAME).is_file())
            self.assertEqual(json.loads((self.root/'audits/current.json').read_bytes())['counts'],[1])
            self.acked.append(ticket)
        self.service.complement=SimpleNamespace(prepared_source=prepared,acknowledge_preparation=acknowledge)
        def compute(*_):self.computations+=1;return self.raw
        self.source=SimpleNamespace(run_ticket=compute,finalize_report=self.service.finalize_prepared_report,
            retain_audit_partition=self.service.retain_audit_partition,acknowledge_preparation=self.service.acknowledge_prepared_source)
        running=threading.Event();running.set()
        self.audit=NetworkAudits(SimpleNamespace(root=self.root,running=running,source=self.source,
            args=SimpleNamespace(piece_limit=10**12)),before_commit=lambda _:False)
        self.audit.state=lambda _:dict(jobs=[dict(slot=0,attempt=0,state='assigned')])
        (self.root/'audits/current.json').write_text(json.dumps(self.input))
        self.frame=dict(ticket_id=cfg['ticket_id'],start=cfg['start'],end=cfg['end'],positions=str(int(cfg['end'])-int(cfg['start'])),
            phase='Report ready',scopes=dict(ticket=dict(statistics=dict(count_total=0))))

    def compact(self):
        with patch('horizon_status_stream.ticket_frame',return_value=self.frame):return archive.compact(self.folder)

    def test_audit_count_handoff_frees_raw_index_without_a_search_report_or_native_pass(self):
        self.assertEqual(self.audit.process_one(),'paused');self.assertEqual(len(self.acked),1)
        self.assertFalse((self.folder/'network-report.json').exists());self.assertFalse((self.folder/'preseal-input.json').exists())
        self.assertTrue(self.compact()['compacted']);body=archive.load(self.folder)
        self.assertEqual(body['report']['body']['best'][0]['gap'],956)
        self.assertEqual(body['scientific_finalization']['handoff_kind'],'audit_count')
        self.assertEqual(body['frame']['work_role'],'audit')
        standalone=archive.frame(self.folder)
        self.assertEqual(standalone['work_role'],'audit');self.assertEqual(standalone['record_pairs'],[])
        self.assertFalse(body['report']['network_accepted']);self.assertFalse((self.folder/'session-index.sqlite3').exists())
        self.assertTrue(archive.compact(self.folder)['already_compacted'])

    def test_restart_reuses_recorded_count_and_compact_result_without_gpu_or_source_rescan(self):
        self.audit.process_one();self.compact();before=(self.folder/NAME).read_bytes()
        self.assertEqual(self.audit.process_one(),'paused');self.assertEqual(self.computations,1)
        self.assertEqual((self.folder/NAME).read_bytes(),before);self.assertEqual(len(self.acked),2)

    def test_crash_after_count_before_handoff_recovers_without_recomputation(self):
        with patch.object(self.source,'retain_audit_partition',side_effect=OSError('Controlled handoff crash')):
            with self.assertRaises(OSError):self.audit.process_one()
        self.assertEqual(self.computations,1);self.assertEqual(self.acked,[])
        self.assertFalse(self.compact()['compacted']);self.assertTrue((self.folder/'session-index.sqlite3').exists())
        self.assertEqual(self.audit.process_one(),'paused');self.assertEqual(self.computations,1)
        self.assertTrue(self.compact()['compacted'])

    def test_changed_recorded_count_cannot_release_source_or_raw_index(self):
        self.audit.process_one();file=self.root/'audits/current.json';value=json.loads(file.read_bytes());value['counts']=[2];file.write_text(json.dumps(value))
        with self.assertRaises(ValueError):self.audit.process_one()
        self.assertEqual(len(self.acked),1);self.assertTrue((self.folder/'session-index.sqlite3').exists())

    def test_rebound_slot_with_a_fresh_checksum_still_fails_exact_ticket_binding(self):
        self.audit.process_one();file=self.folder/NAME;value=json.loads(file.read_bytes());value['audit']['slot']=1
        raw=archive.canonical(value)+b'\n';file.write_bytes(raw);(self.folder/(NAME+'.sha256')).write_bytes(hashlib.sha256(raw).hexdigest().encode()+b'\n')
        with self.assertRaises(ValueError):self.compact()
        self.assertTrue((self.folder/'session-index.sqlite3').exists())

    def test_audit_archive_cannot_claim_search_even_with_rewritten_archive_checksum(self):
        self.audit.process_one();self.compact();body=archive.load(self.folder)
        body['frame']['work_role']='search'
        raw=gzip.compress(archive.canonical(body),mtime=0);(self.folder/archive.ARCHIVE_NAME).write_bytes(raw)
        file=self.folder/'session.json';marker=json.loads(file.read_bytes());marker['sha256']=hashlib.sha256(raw).hexdigest();file.write_bytes(archive.canonical(marker))
        with self.assertRaises(ValueError):archive.frame(self.folder)

    def legacy_role(self,compact_first,interrupted_checksum=False):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);cfg={k:v for k,v in self.cfg.items()if k not in('preseal_required','complement_feed_sha256')}
            folder=root/'tickets'/cfg['ticket_id'];raw=indexed_report(cfg,[])
            store=SessionStore.create(folder,cfg,dict(uuid='NO-HARDWARE-AUDIT-ATTRIBUTION'))
            try:store.accepted_progress(int(cfg['end'])-int(cfg['start']),0);store.closed('COMPLETE',0,0)
            finally:store.close()
            (folder/'report.json').write_bytes(archive.canonical(raw))
            service=EngineService.__new__(EngineService);service.root=root
            service.archives=SimpleNamespace(use_ticket=lambda _:nullcontext(),offer=lambda _:None)
            value=dict(self.input,counts=[0]);frame=dict(self.frame)
            if compact_first:
                with patch('horizon_status_stream.ticket_frame',return_value=frame):self.assertTrue(archive.compact(folder)['compacted'])
                before=(folder/archive.ARCHIVE_NAME).read_bytes()
            if interrupted_checksum:
                from pgn_session import atomic_bytes
                def interrupted(file,data):
                    if Path(file).name==NAME+'.sha256':raise OSError('Controlled checksum interruption')
                    atomic_bytes(file,data)
                with patch('horizon_audit_partition_archive.atomic_bytes',side_effect=interrupted):
                    with self.assertRaises(OSError):service.retain_audit_partition(value,self.part,None)
                with self.assertRaises((ValueError,OSError)):archive.frame(folder)
            service.retain_audit_partition(value,self.part,None)
            if compact_first:self.assertEqual((folder/archive.ARCHIVE_NAME).read_bytes(),before)
            else:
                with patch('horizon_status_stream.ticket_frame',return_value=frame):self.assertTrue(archive.compact(folder)['compacted'])
            result=archive.frame(folder);self.assertEqual(result['work_role'],'audit');self.assertEqual(result['record_pairs'],[])
            service.retain_audit_partition(value,self.part,None)
            self.assertEqual(archive.frame(folder)['work_role'],'audit')
            (folder/(NAME+'.sha256')).write_bytes(b'0'*64+b'\n')
            with self.assertRaises(ValueError):archive.frame(folder)

    def test_legacy_audit_role_survives_compaction_without_scientific_admission(self):
        self.legacy_role(False)

    def test_compaction_before_audit_handoff_still_recovers_correct_immutable_role(self):
        self.legacy_role(True)

    def test_partial_audit_identity_checksum_after_compaction_replays_without_recomputation(self):
        self.legacy_role(True,True)


if __name__=='__main__':unittest.main()
