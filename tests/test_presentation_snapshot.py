"""Real SQLite read-only frames: conserved values, identity and fresh changes."""
from pathlib import Path
from contextlib import closing
import hashlib
import importlib.util
import json
import sqlite3
import statistics
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT/'src/engine/runtime_host'), str(ROOT/'src/protocol')]
import horizon_status_stream as stream
from horizon_engine_host import IMAGES
from pgn_index import read_metadata_snapshot, read_presentation_snapshot
from pgn_threshold_policy import pin_range


class Fixture:
    def __init__(self, root):
        self.folder = root/'tickets'/('a'*64)
        self.folder.mkdir(parents=True)
        start = 150_000_000_000_000_000_000
        end = start + 200_000_000_000_000
        cfg = dict(ticket_id='a'*64, start=str(start), end=str(end), target_merit=29,
                   threshold_policy=pin_range(start, end))
        meta = dict(configuration=cfg, state='RUNNING',
                    current_chunk=dict(identity=dict(worker_sha256=next(iter(IMAGES.values())))))
        (self.folder/'session.json').write_text(json.dumps(meta), encoding='utf-8')
        self.path = self.folder/'session-index.sqlite3'
        top = [[1000+i*2, str(start+1000+i*1200), i] for i in range(100)]
        self.values = dict(binding=dict(configuration_hash=hashlib.sha256(
                              json.dumps(cfg,sort_keys=True,separators=(',',':')).encode()).hexdigest()),
                           progress=dict(positions='200000', tests='100000'),
                           projection=dict(prefix=str(start+200000), count_top=top, recent=top[::-1],
                               count_total=100, threshold_counts={'954':100},
                               merit_band_counts={'21':100}, at_or_above_target=0,
                               statistics_target=29),
                           session_metadata=dict(closed_chunks=['x'*1024]*50),
                           journal_sequence=9, state='RUNNING', timing={}, active_chunk={},
                           next_range={}, prepared_replacement={}, completed_chunk_count=50)
        with closing(sqlite3.connect(self.path)) as db, db:
            db.execute('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT NOT NULL)')
            db.executemany('INSERT INTO meta VALUES(?,?)', [(k,json.dumps(v)) for k,v in self.values.items()])

    def put(self, **values):
        with closing(sqlite3.connect(self.path)) as db, db:
            db.executemany('UPDATE meta SET v=? WHERE k=?', [(json.dumps(v),k) for k,v in values.items()])


def stable_frame(module, folder):
    value = module.ticket_frame(folder, 7, 42)
    value.pop('observed_utc')
    return value


class SnapshotTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.fixture = Fixture(Path(self.temp.name))

    def test_selected_fields_equal_complete_atomic_snapshot(self):
        full = read_metadata_snapshot(self.fixture.path)
        self.assertEqual(read_presentation_snapshot(self.fixture.path),
                         {k:full[k] for k in ('binding','progress','projection')})

    def test_frame_values_equal_full_reader_and_read_never_changes_database(self):
        original = self.fixture.path.read_bytes()
        actual = stable_frame(stream, self.fixture.folder)
        with patch.object(stream, 'read_presentation_snapshot', read_metadata_snapshot):
            baseline = stable_frame(stream, self.fixture.folder)
        self.assertEqual(actual, baseline)
        self.assertEqual(self.fixture.path.read_bytes(), original)
        self.assertEqual(actual['record_pairs'],actual['scopes']['ticket']['best'])

    def test_next_committed_frontier_and_new_gap_are_immediate(self):
        first = stable_frame(stream,self.fixture.folder)
        projection = dict(self.fixture.values['projection'])
        projection['prefix'] = str(int(projection['prefix'])+100)
        projection['count_top'] = [[1400,str(int(first['start'])+50),101]]
        self.fixture.put(progress=dict(positions='200100',tests='100500'),projection=projection)
        second = stable_frame(stream,self.fixture.folder)
        self.assertEqual(second['positions'],'200100')
        self.assertEqual(second['tests'],'100500')
        self.assertEqual(second['record_pairs'][0]['gap'],1400)

    def test_rollback_is_visible_without_a_cached_projection(self):
        stable_frame(stream,self.fixture.folder)
        projection = dict(self.fixture.values['projection'],prefix='150000000000000000000',count_top=[],recent=[])
        self.fixture.put(progress=dict(positions='0',tests='0'),projection=projection)
        second = stable_frame(stream,self.fixture.folder)
        self.assertEqual(second['positions'],'0')
        self.assertEqual(second['record_pairs'],[])

    def test_mismatched_configuration_and_frontier_still_fail(self):
        self.fixture.put(binding=dict(configuration_hash='0'*64))
        with self.assertRaisesRegex(ValueError,'different identities'):
            stable_frame(stream,self.fixture.folder)
        self.fixture.put(binding=self.fixture.values['binding'],progress=dict(positions='1',tests='1'))
        with self.assertRaisesRegex(ValueError,'frontier'):
            stable_frame(stream,self.fixture.folder)

    def test_required_metadata_budget_is_preserved(self):
        self.fixture.put(projection='x'*1048576)
        with self.assertRaisesRegex(ValueError,'Oversized'):
            read_presentation_snapshot(self.fixture.path)

    def test_recovery_metadata_remains_bounded_on_its_original_reader(self):
        expected = read_presentation_snapshot(self.fixture.path)
        self.fixture.put(session_metadata='x'*1048576)
        self.assertEqual(read_presentation_snapshot(self.fixture.path),expected)
        with self.assertRaisesRegex(ValueError,'Oversized'):
            read_metadata_snapshot(self.fixture.path)

    def test_missing_read_does_not_create_database(self):
        missing = Path(self.temp.name)/'absent.sqlite3'
        self.assertEqual(read_presentation_snapshot(missing),{})
        self.assertFalse(missing.exists())


def benchmark(before, output):
    spec=importlib.util.spec_from_file_location('retained_before_status_stream',before)
    old=importlib.util.module_from_spec(spec)
    spec.loader.exec_module(old)
    with tempfile.TemporaryDirectory() as temp:
        fixture=Fixture(Path(temp))
        assert stable_frame(old,fixture.folder)==stable_frame(stream,fixture.folder)
        timings=[]
        for trial in range(3):
            pair={}
            for label,module in ([('old',old),('new',stream)] if trial%2==0 else [('new',stream),('old',old)]):
                began=time.perf_counter_ns();cpu=time.process_time_ns()
                for _ in range(200):
                    packet=json.dumps(module.ticket_frame(fixture.folder,7,42),allow_nan=False)
                pair[label]=dict(wall_ns=time.perf_counter_ns()-began,cpu_ns=time.process_time_ns()-cpu,
                                 packet_bytes=len(packet.encode()))
            timings.append(pair)
    result=dict(PASS=True,scope='3 paired runs of 200 actual SQLite frames plus JSON serialization; synthetic 100 retained rows and 50KiB recovery metadata',
                GPU_used=False,devices_queried=0,source_values_equal=True,measurements=timings,
                median_old_cpu_ns=statistics.median(x['old']['cpu_ns'] for x in timings),
                median_new_cpu_ns=statistics.median(x['new']['cpu_ns'] for x in timings))
    Path(output).write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(result))


if __name__=='__main__':
    if len(sys.argv)>1 and sys.argv[1]=='--benchmark':
        benchmark(*sys.argv[2:])
    else:
        unittest.main()
