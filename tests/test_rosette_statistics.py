import copy,math,tempfile,unittest,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ('src/engine/runtime_host','src/engine/shared_composite','src/protocol')]
from pgn_rosette_statistics import add_rosette,empty_rosettes
from pgn_projection import Projection
from pgn_session import SessionStore
from pgn_statistics import statistics_state
from pgn_native_contract import fields

class RosetteStatisticsTests(unittest.TestCase):
    def test_exclusive_fractional_boundaries_without_rounding(self):
        for target in (26.4,27.5,28.06,33.97):
            for offset,key in enumerate(('gold','silver','bronze','rust')):
                counts=empty_rosettes();cutoff=target-offset
                add_rosette(counts,cutoff,target)
                self.assertEqual(counts[key],1);self.assertEqual(sum(counts.values()),1)
                add_rosette(counts,cutoff,target,-1);self.assertEqual(counts,empty_rosettes())
                add_rosette(counts,math.nextafter(cutoff,-math.inf),target)
                self.assertEqual(counts[('silver','bronze','rust','ordinary')[offset]],1)
    def test_bad_values_fail_explicitly(self):
        for merit,target in ((math.nan,27.5),(26,math.inf),(-1,28),(28,0)):
            with self.assertRaises(ValueError):add_rosette(empty_rosettes(),merit,target)
    def make_store(self):
        tmp=tempfile.TemporaryDirectory();self.addCleanup(tmp.cleanup);self.folder=Path(tmp.name)/'session'
        self.start=154000000000000000001;self.width=100000
        self.config=dict(start=str(self.start),end=str(self.start+self.width),bins=100,report_top=100,**fields(self.start,self.start+self.width))
        self.config['target_merit']=26.4
        return SessionStore.create(self.folder,self.config,{'uuid':'NO-HARDWARE-STATISTICS-QA'})
    def test_full_incremental_projection_rebuild_migration_and_reopen(self):
        store=self.make_store();pairs=[]
        try:
            for i,gap in enumerate((954,1000,1100,1164,1242,1400)):
                lo=self.start+i*5000;pairs.append((lo,lo+gap));store.record_gap(lo,lo+gap,'SYNTHETIC_PRESENTATION_GEOMETRY')
            store.accepted_progress(self.width,0)
            value=copy.deepcopy(store.gaps.projection.value)
            expected=empty_rosettes()
            for lo,hi in pairs:add_rosette(expected,(hi-lo)/math.log(lo),26.4)
            self.assertEqual(value['rosette_counts'],expected)
            self.assertEqual(sum(expected.values()),value['count_total'])
            self.assertEqual(expected['gold'],value['at_or_above_target'])
            projection=store.gaps.projection
            rolled=projection.move(self.start+10000);projection.value=rolled
            partial=empty_rosettes()
            for lo,hi in pairs[:2]:add_rosette(partial,(hi-lo)/math.log(lo),26.4)
            self.assertEqual(rolled['rosette_counts'],partial)
            forward=projection.move(self.start+self.width);projection.value=forward
            self.assertEqual(forward['rosette_counts'],expected)
            before={key:value[key]for key in ('counts','count_total','detail_total','threshold_counts','count_top','recent')}
            # An old derived projection can be rebuilt from the durable complete
            # prefix; it is never reconstructed from a bounded Best/Recent list.
            legacy=copy.deepcopy(value);legacy.pop('rosette_counts');store.gaps.put_meta('projection',legacy);store.gaps.db.commit()
            migrated=Projection(store.gaps,self.config).value
            self.assertEqual(migrated['rosette_counts'],expected)
            self.assertEqual(before,{key:migrated[key]for key in before})
            self.assertEqual(statistics_state(migrated)['rosette_counts'],expected)
            store.close();store=SessionStore.load(self.folder)
            self.assertEqual(store.gaps.projection.value['rosette_counts'],expected)
            # Repeated observed gap identities must not manufacture rosettes.
            lo,hi=pairs[-1];store.record_gap(lo,hi,'SYNTHETIC_PRESENTATION_GEOMETRY')
            self.assertEqual(store.gaps.projection.value['rosette_counts'],expected)
        finally:store.close()
    def test_repaired_presentation_matches_all_pairs_and_original_target(self):
        from pgn_gap_projection import presentation
        store=self.make_store()
        try:
            pairs=[(self.start+i*5000,self.start+i*5000+gap)for i,gap in enumerate((954,1100,1164,1242,1400))]
            projected=presentation(pairs,self.config)
            expected=empty_rosettes()
            for lo,hi in pairs:add_rosette(expected,(hi-lo)/math.log(lo),26.4)
            self.assertEqual(projected['statistics']['rosette_counts'],expected)
            self.assertEqual(sum(expected.values()),projected['statistics']['count_total'])
            changed=dict(self.config,target_merit=29.3)
            self.assertNotEqual(presentation(pairs,changed)['statistics']['rosette_counts'],expected)
            self.assertEqual(projected['statistics']['statistics_target'],26.4)
        finally:store.close()

if __name__=='__main__':unittest.main()
