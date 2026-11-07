"""Real SQLite/session/queue/archive with synthetic gap geometry, no mining claim."""
from pathlib import Path
from contextlib import closing
from contextlib import nullcontext
from types import SimpleNamespace
import copy,hashlib,json,sys,tempfile,unittest,sqlite3
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ('src/engine/runtime_host','src/engine/shared_composite','src/protocol','tests')]
import horizon_ticket_archive as archive
from pgn_session import SessionStore
from pgn_statistics import statistics_state
from horizon_status_stream import rows
from horizon_preseal_queue import PresealQueue
from horizon_report_outbox import ReportOutbox
from test_report_gap_repair import configuration,indexed_report


class ArchiveReconciliationTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name);self.folder=self.root/'tickets'/('a'*64)
        a=150000000000000000001;self.cfg=configuration(a,a+200000000000000);self.cfg['preseal_required']=True
        self.true=[(a+2010*i,a+2010*(i+1))for i in range(110)]
        lo,hi=self.true[-1];self.added=[(lo,hi)];self.removed=[(lo,lo+1004),(lo+1004,hi)]
        self.old=self.true[:-1]+self.removed
        self.raw=indexed_report(self.cfg,self.old);self.corrected=indexed_report(self.cfg,self.true)
        self.work=dict(ticket=self.folder.name,request_id='b'*64,work_id='c'*64,genesis='d'*64,
            start=self.cfg['start'],length='200000000000000',target_numerator='29',target_denominator='1')
        store=SessionStore.create(self.folder,self.cfg,dict(uuid='NO-HARDWARE-ARCHIVE-GEOMETRY'))
        try:
            for x,y in self.old:store.record_gap(x,y,'SYNTHETIC_ARCHIVE_GEOMETRY')
            store.accepted_progress(int(self.work['length']),0);store.closed('COMPLETE',0,0)
            projection=store.gaps.projection.value
            self.frame=dict(ticket_id=self.folder.name,start=self.cfg['start'],end=self.cfg['end'],
                positions=self.work['length'],phase='Report ready',observed_utc='2026-11-06T00:00:00+00:00',
                scopes=dict(ticket=dict(statistics=statistics_state(projection),best=rows(projection['count_top'],100),recent=rows(projection['recent'],30))))
        finally:store.close()
        (self.folder/'report.json').write_bytes(archive.canonical(self.raw))
        self.marker=dict(schema='PGN_SCOPED_SOURCE_FINALIZATION_V1',ticket_id=self.folder.name,
            original_report_sha256=self.raw['sha256'],corrected_report=self.corrected,
            configuration_sha256=hashlib.sha256(archive.canonical(self.cfg)).hexdigest(),
            added=[[str(a),str(b)]for a,b in self.added],removed=[[str(a),str(b)]for a,b in self.removed],
            source=dict(controlled_geometry_fixture=True,whole95_coverage=False))
        self.write_marker()
        self.queue=PresealQueue(self.root);self.outbox=ReportOutbox(self.root)
        self.queue.put(self.work,self.raw);self.queue.pump(lambda *_:copy.deepcopy(self.corrected),self.outbox)
        self.original=(self.folder/'session.json').read_bytes()

    def write_marker(self):
        raw=archive.canonical(self.marker)+b'\n';(self.folder/'source-finalization.json').write_bytes(raw)
        (self.folder/'source-finalization.sha256').write_bytes(hashlib.sha256(raw).hexdigest().encode()+b'\n')

    def compact(self,**kw):
        with patch('horizon_status_stream.ticket_frame',return_value=self.frame):return archive.compact(self.folder,**kw)

    def intact(self):
        self.assertEqual((self.folder/'session.json').read_bytes(),self.original)
        self.assertTrue((self.folder/'session-index.sqlite3').is_file())
        self.assertEqual(json.loads((self.folder/'report.json').read_bytes()),self.raw)

    def test_full_correction_outside_best100_survives_compaction_and_restart(self):
        self.assertNotIn(str(self.added[0][0]),[r['lower_prime']for r in self.corrected['body']['best']])
        outcome=self.compact();self.assertTrue(outcome['compacted'])
        body=archive.load(self.folder);self.assertEqual(body['report'],self.corrected)
        self.assertEqual(body['scientific_finalization']['original_pairs'],111)
        self.assertEqual(body['scientific_finalization']['corrected_pairs'],110)
        self.assertEqual(body['frame']['scopes']['ticket']['statistics']['count_total'],110)
        self.assertEqual(body['frame']['scopes']['ticket']['recent_order'],'lower_prime_descending')
        self.assertEqual(body['frame']['scopes']['ticket']['recent'][0]['lower_prime'],str(self.added[0][0]))
        self.assertFalse((self.folder/'session-index.sqlite3').exists())
        self.assertEqual(json.loads((self.folder/'report.json').read_bytes()),self.raw)
        self.assertTrue(archive.compact(self.folder)['already_compacted'])

    def test_exact_all_gap_observations_and_correction_reconstruct_final_set(self):
        import gzip
        self.assertTrue(self.compact()['compacted'])
        body=archive.load(self.folder);descriptor=body['scientific_gap_archive']
        from horizon_scientific_gaps import NAME,verify
        verify(self.folder,descriptor,self.cfg)
        with gzip.open(self.folder/NAME,'rt',encoding='utf-8')as stream:
            rows=[json.loads(line)for line in stream]
        observed={(int(row[0]),int(row[1]))for row in rows[1:-1]}
        self.assertEqual(observed,set(self.old));self.assertEqual(descriptor['records'],111)
        reconstructed=(observed-set(self.removed))|set(self.added)
        self.assertEqual(reconstructed,set(self.true))
        self.assertFalse((self.folder/'session-index.sqlite3').exists())

    def test_scientific_corruption_after_marker_keeps_recoverable_raw_index(self):
        from horizon_scientific_gaps import NAME
        with patch.object(archive,'_purge_work_files',side_effect=RuntimeError('Crash after archive marker')):
            with self.assertRaisesRegex(RuntimeError,'after archive marker'):self.compact()
        source=self.folder/'session-index.sqlite3';original=source.read_bytes()
        path=self.folder/NAME;raw=bytearray(path.read_bytes());raw[len(raw)//2]^=1;path.write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'fingerprint'):archive.compact(self.folder)
        self.assertEqual(source.read_bytes(),original)

    def test_legacy_archive_without_pair_descriptor_keeps_remaining_source(self):
        import gzip
        with patch.object(archive,'_purge_work_files',side_effect=RuntimeError('Crash after archive marker')):
            with self.assertRaises(RuntimeError):self.compact()
        body=archive.load(self.folder);del body['scientific_gap_archive']
        raw=gzip.compress(archive.canonical(body),mtime=0);(self.folder/archive.ARCHIVE_NAME).write_bytes(raw)
        marker=json.loads((self.folder/'session.json').read_bytes());marker['sha256']=hashlib.sha256(raw).hexdigest()
        (self.folder/'session.json').write_bytes(archive.canonical(marker))
        outcome=archive.compact(self.folder);self.assertFalse(outcome['compacted']);self.assertIn('remaining source retained',outcome['reason'])
        self.assertTrue((self.folder/'session-index.sqlite3').is_file())

    def test_dry_run_does_not_edit_the_original_index(self):
        self.assertTrue(self.compact(dry_run=True)['dry_run']);self.intact()

    def test_restart_of_corrected_archive_reuses_original_preseal_input(self):
        self.compact();corrected=archive.result(self.folder,self.cfg)
        from horizon_engine_host import EngineService
        service=EngineService.__new__(EngineService);service.root=self.root
        service.archives=SimpleNamespace(use_ticket=lambda _:nullcontext())
        def no_source(*_):raise AssertionError('Retired source or deleted raw index was reopened')
        service.complement=SimpleNamespace(prepared_source_for=no_source)
        self.assertEqual(service.finalize_prepared_report(self.work,corrected),corrected)
        self.queue.put(self.work,corrected)
        def no_recompute(*_):raise AssertionError('Already finalized archive was recomputed')
        self.assertTrue(self.queue.pump(no_recompute,self.outbox));self.assertEqual(self.queue.entries(),[])
        self.assertEqual(json.loads((self.folder/'preseal-input.json').read_bytes())['computed'],self.raw)

    def test_changed_full_delta_fails_before_any_deletion(self):
        self.marker['added'][0][1]=str(self.added[0][1]+2);self.write_marker()
        with self.assertRaises(ValueError):self.compact()
        self.intact()

    def test_removed_pair_must_exist_in_the_original_complete_index(self):
        self.marker['removed'][0][0]=str(self.removed[0][0]+2);self.write_marker()
        with self.assertRaises(ValueError):self.compact()
        self.intact()

    def test_wrong_marker_configuration_is_rejected(self):
        self.marker['configuration_sha256']='f'*64;self.write_marker()
        with self.assertRaises(ValueError):self.compact()
        self.intact()

    def test_missing_checksum_holds_the_original_index(self):
        (self.folder/'source-finalization.sha256').unlink()
        self.assertFalse(self.compact()['compacted']);self.intact()

    def test_incomplete_index_frontier_is_rejected_even_with_complete_label(self):
        # Fault injection below the session API, which correctly forbids a
        # retreat. This deliberately corrupts only this synthetic fixture.
        with closing(sqlite3.connect(self.folder/'session-index.sqlite3'))as db:
            with db:
                value=json.loads(db.execute("SELECT v FROM meta WHERE k='progress'").fetchone()[0])
                value['positions']='100'
                db.execute("UPDATE meta SET v=? WHERE k='progress'",(json.dumps(value),))
        with self.assertRaises(ValueError):self.compact()
        self.assertTrue((self.folder/'session-index.sqlite3').exists())

    def test_crash_before_archive_marker_retains_original_then_recovers(self):
        real=archive.atomic_bytes
        def fail(path,data):
            if Path(path).name=='session.json':raise OSError('Controlled archive commit crash')
            real(path,data)
        with patch('horizon_ticket_archive.atomic_bytes',side_effect=fail),self.assertRaises(OSError):self.compact()
        self.intact();self.assertTrue(self.compact()['compacted'])

    def test_crash_after_marker_returns_corrected_report_then_drains_redundant_files(self):
        with patch('horizon_ticket_archive._purge_work_files',side_effect=OSError('Controlled purge crash')),self.assertRaises(OSError):self.compact()
        self.assertEqual(archive.result(self.folder,self.cfg),self.corrected)
        self.assertTrue((self.folder/'session-index.sqlite3').exists())
        self.assertTrue(archive.compact(self.folder)['already_compacted'])
        self.assertFalse((self.folder/'session-index.sqlite3').exists())

    def test_post_commit_marker_change_cannot_be_hidden_by_valid_archive_hash(self):
        self.compact();self.marker['added'][0][1]=str(self.added[0][1]+2);self.write_marker()
        with self.assertRaises(ValueError):archive.load(self.folder)


if __name__=='__main__':unittest.main()
