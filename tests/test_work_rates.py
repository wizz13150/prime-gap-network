"""Checkpoint restoration is not a fresh work-rate timing baseline."""
from pathlib import Path
import sys
import unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from pgn_publications import WorkRates

class WorkRateTests(unittest.TestCase):
    def test_restore_then_immediate_publication_does_not_create_a_speed_spike(self):
        rates=WorkRates();base=12_369_505_812_460;tests=31_977_648_498
        rates.begin_piece(minimum_positions=base,minimum_tests=tests)
        rates.add([(1_000_000_000,base,tests),(1_000_800_000,base+128_000_000_000,tests+320_000_000)])
        self.assertIsNone(rates.view()['speed_rate'])
        self.assertEqual(rates.view()['observed_active_seconds'],0)
        rates.add([(2_000_800_000,base+1_128_000_000_000,tests+2_920_000_000)])
        self.assertEqual(rates.view()['speed_rate'],1_000_000_000_000)
        self.assertEqual(rates.view()['compute_tests_rate'],2_600_000_000)

    def test_pre_restore_and_equal_restore_samples_never_establish_baseline(self):
        rates=WorkRates();rates.begin_piece(minimum_positions=1000,minimum_tests=100)
        rates.add([(10,0,0),(20,900,90),(30,1000,100),(40,1000,100),(1_000_000_000,1001,101)])
        self.assertIsNone(rates.view()['speed_rate'])
        rates.add([(2_000_000_000,1002,103)])
        self.assertEqual(rates.view()['speed_rate'],1)
        self.assertEqual(rates.view()['compute_tests_rate'],2)

    def test_fresh_piece_needs_two_advancing_observations(self):
        rates=WorkRates();rates.begin_piece()
        rates.add([(0,0,0),(1_000_000_000,10,20)])
        self.assertIsNone(rates.view()['speed_rate'])
        rates.add([(2_000_000_000,20,40)])
        self.assertEqual(rates.view()['speed_rate'],10)

    def test_resident_handoff_preserves_qualified_window_without_counting_restore(self):
        rates=WorkRates();rates.begin_piece()
        rates.add([(0,10,20),(1_000_000_000,20,40)])
        self.assertEqual(rates.view()['speed_rate'],10)
        rates.begin_piece(preserve_window=True,minimum_positions=1000,minimum_tests=2000)
        rates.add([(2_000_000_000,1000,2000),(2_000_100_000,1010,2020)])
        self.assertEqual(rates.view()['speed_rate'],10)
        rates.add([(3_000_100_000,1020,2040)])
        self.assertEqual(rates.view()['average_rate'],10)

    def test_regressing_live_counter_still_fails(self):
        rates=WorkRates();rates.begin_piece()
        rates.add([(1_000_000_000,10,20)])
        with self.assertRaises(ValueError):rates.add([(2_000_000_000,9,21)])

    def test_new_instance_has_no_old_speed(self):
        rates=WorkRates();rates.begin_piece();rates.add([(1_000_000_000,10,20),(2_000_000_000,20,40)])
        rates.begin_piece(minimum_positions=20,minimum_tests=40)
        self.assertIsNone(rates.view()['speed_rate'])

    def test_asynchronous_descriptor_burst_waits_for_a_real_one_second_window(self):
        rates=WorkRates();rates.begin_piece(minimum_positions=1000,minimum_tests=100)
        rates.add([(1_000_000_000,64_000_001_000,160_000_100),
                   (1_000_499_400,128_000_001_000,320_000_100)])
        warming=rates.view()
        for key in ('speed_rate','average_rate','compute_tests_rate','eta_rate'):
            self.assertIsNone(warming[key],key)
        rates.add([(2_000_000_000,1_064_000_001_000,2_760_000_100)])
        self.assertEqual(rates.view()['speed_rate'],1_000_000_000_000)
        self.assertEqual(rates.view()['compute_tests_rate'],2_600_000_000)

if __name__=='__main__':unittest.main()
