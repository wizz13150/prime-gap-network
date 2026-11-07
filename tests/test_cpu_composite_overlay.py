"""Native complement admission and owned-count regression; no GPU or service."""
import ctypes
import json
import math
import os
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

WORKER, MAPPER = map(str, sys.argv[1:3])
del sys.argv[1:3]
MASK = (1 << 64) - 1
LOWER = 160696461657079905863
START = LOWER - 10000
ORIGIN = START // 30 * 30
LENGTH = 2000000 + START - ORIGIN
FACTOR, COFACTOR = 74995840921, 2142738313
COMPOSITE = FACTOR * COFACTOR
CLASSES = [1, 7, 11, 13, 17, 19, 23, 29]


def words(n):
    return n & MASK, n >> 64


def selected_cpu():
    if os.name != 'nt':
        return min(os.sched_getaffinity(0))
    kernel = ctypes.WinDLL('kernel32', use_last_error=True)
    kernel.GetCurrentProcess.restype = ctypes.c_void_p
    kernel.GetProcessAffinityMask.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.POINTER(ctypes.c_size_t)]
    mask, system = ctypes.c_size_t(), ctypes.c_size_t()
    if not kernel.GetProcessAffinityMask(kernel.GetCurrentProcess(), ctypes.byref(mask), ctypes.byref(system)):
        raise OSError(ctypes.get_last_error())
    return (mask.value & -mask.value).bit_length() - 1


class CompositeOverlayTest(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='pgn-cpu-composite-')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.base = [WORKER, '--owned-piece', '--cpu', str(selected_cpu())]

    def page(self, name, start=ORIGIN, length=LENGTH, rows=None, tail=b''):
        if rows is None:
            rows = [(COMPOSITE, FACTOR, COFACTOR)]
        target = self.root / (name + '.pgncmp')
        target.write_bytes(struct.pack('<8s4Q', b'PGNCMP01', *words(start), length, len(rows)) +
                           b''.join(struct.pack('<5Q', n-start, *words(p), *words(q)) for n, p, q in rows) + tail)
        return target

    def options(self, path, start=ORIGIN, length=LENGTH):
        lo, hi = words(start)
        return ['--composite-cohort', str(hi), str(lo), str(length), '--composite-page', str(path)]

    def invoke(self, options=(), command=None, okay=True):
        if command is None:
            command = '\n'.join(f'TASK {i} {words(START+i*20000)[1]} {words(START+i*20000)[0]} 20000 954' for i in range(100)) + '\nSTOP\n'
        p = subprocess.run([*self.base, *options], input=command, encoding='utf-8', capture_output=True, timeout=20)
        self.assertLess(len(p.stdout) + len(p.stderr), 1024 * 1024)
        records = [json.loads(line) for line in p.stdout.splitlines()]
        results = [row for row in records if row.get('type') == 'result']
        if okay:
            self.assertEqual(p.returncode, 0, p.stderr)
        else:
            self.assertEqual(p.returncode, 1, p.stderr)
            self.assertEqual(results, [])
        return results

    def test_actual_walk_recovers_interior_case_and_preserves_partition_ownership(self):
        baseline = self.invoke()
        self.assertEqual([int(x['count']) for x in baseline], [0]*100)
        path = self.page('known')
        for extra in ([], ['--tiled'], ['--tiled', '--small-factor-complement']):
            with self.subTest(extra=extra):
                rows = self.invoke([*self.options(path), *extra])
                self.assertEqual([int(x['count']) for x in rows], [1]+[0]*99)
                self.assertEqual(rows[0]['best'], [dict(lower_prime=str(LOWER), upper_prime=str(LOWER+956), gap=956)])
                self.assertTrue(all(int(x['positions']) == 20000 for x in rows))
                self.assertTrue(all(not x['production_admitted'] and not x['complement_complete_psp_coverage'] for x in rows))

    def test_minimum_is_applied_after_recovery(self):
        path = self.page('known')
        for minimum, expected in ((956, 1), (960, 0)):
            lo, hi = words(START)
            row = self.invoke(self.options(path), f'TASK 1 {hi} {lo} 20000 {minimum}\nSTOP\n')[0]
            self.assertEqual(int(row['count']), expected)

    def test_avx2_when_the_pinned_cpu_supports_it(self):
        path = self.page('known')
        lo, hi = words(START)
        p = subprocess.run([*self.base, *self.options(path), '--tiled', '--simd4'],
                           input=f'TASK 1 {hi} {lo} 20000 954\nSTOP\n', encoding='utf-8', capture_output=True, timeout=10)
        if p.returncode == 1 and 'AVX2 not admitted on pinnedCPU/OS' in p.stderr:
            self.skipTest('Pinned CPU/OS does not admit AVX2')
        self.assertEqual(p.returncode, 0, p.stderr)
        result = next(json.loads(x) for x in p.stdout.splitlines() if '"type":"result"' in x)
        self.assertEqual(int(result['count']), 1)
        self.assertEqual(result['implementation'], 'shared_avx2_8owner')

    def test_all_declared_cpu_modes_compose_with_large_page_policy(self):
        path = self.page('all-modes')
        lo, hi = words(START)
        command = f'TASK 1 {hi} {lo} 20000 954\nMEMORY\nSTOP\n'
        for page_mode in ('off', 'auto'):
            options = [*self.options(path), '--tiled', '--simd4',
                       '--small-factor-complement', '--large-pages=' + page_mode]
            p = subprocess.run([*self.base, *options], input=command,
                               encoding='utf-8', capture_output=True, timeout=15)
            if p.returncode == 1 and 'AVX2 not admitted on pinnedCPU/OS' in p.stderr:
                self.skipTest('Pinned CPU/OS does not admit AVX2')
            self.assertEqual(p.returncode, 0, p.stderr)
            rows = [json.loads(x) for x in p.stdout.splitlines()]
            results = [x for x in rows if x.get('type') == 'result']
            self.assertEqual(len(results), 1)
            self.assertEqual(int(results[0]['count']), 1)
            self.assertEqual(results[0]['best'], [dict(lower_prime=str(LOWER),
                upper_prime=str(LOWER + 956), gap=956)])
            self.assertEqual(results[0]['implementation'], 'shared_avx2_8owner')
            self.assertTrue(results[0]['complement_enabled'])
            pages = results[0]['memory_pages']
            self.assertEqual(pages['schema'], 'PGN_CPU_WORKER_MEMORY_PAGES_V1')
            self.assertTrue(pages['compute_buffer_used'])
            if page_mode == 'off':
                self.assertEqual(int(pages['large_page_bytes_in_use']), 0)
            elif int(pages['large_page_bytes_in_use']):
                self.assertTrue(pages['verified'])

    def test_ordinary_1554_gap_is_conserved(self):
        start, length = 150867889250001962310, 24576
        path = self.page('ordinary', start, length, rows=[])
        lo, hi = words(start)
        command = f'TASK 1 {hi} {lo} {length} 954\nSTOP\n'
        baseline = self.invoke(command=command)[0]
        corrected = self.invoke([*self.options(path, start, length), '--tiled', '--small-factor-complement'], command)[0]
        target = dict(lower_prime='150867889250001970513', upper_prime='150867889250001972067', gap=1554)
        self.assertIn(target, baseline['best'])
        self.assertEqual(baseline['best'], corrected['best'])
        self.assertEqual(baseline['count'], corrected['count'])

    def test_false_unit_duplicate_outside_and_trailing_inputs_fail_closed(self):
        cases = [
            ('false', [(COMPOSITE, FACTOR, COFACTOR+1)], b''),
            ('unit', [(COMPOSITE, 1, COMPOSITE)], b''),
            ('duplicate', [(COMPOSITE, FACTOR, COFACTOR)]*2, b''),
            ('outside', [(ORIGIN+LENGTH+1, 7, 11)], b''),
            ('trailing', [(COMPOSITE, FACTOR, COFACTOR)], b'x'),
        ]
        for name, rows, tail in cases:
            with self.subTest(name=name):
                self.invoke(self.options(self.page(name, rows=rows, tail=tail)), okay=False)

    def test_stale_page_and_uncovered_query_fail_before_any_result(self):
        path = self.page('known')
        self.invoke(self.options(path, length=LENGTH+1), okay=False)
        for start, length in ((ORIGIN-1, 1), (ORIGIN, LENGTH+1)):
            lo, hi = words(start)
            self.invoke(self.options(path), f'TASK 1 {hi} {lo} {length} 954\n', okay=False)

    def test_partial_options_and_unsupported_domain_are_not_promoted(self):
        path = self.page('known')
        self.invoke(['--composite-page', str(path)], okay=False)
        self.invoke(['--composite-cohort', str(ORIGIN >> 64), str(ORIGIN & MASK), str(LENGTH)], okay=False)
        self.invoke(command='TASK 1 179811 1 20000 954\n', okay=False)

    def test_shared_page_geometry_all_widths_carries_and_duplicate_or(self):
        commands, expected = [], []
        origins = [((1 << (bits-1))+10**10)//30*30 for bits in range(67, 96)]
        origins.append(((11 << 64)-3000)//30*30)
        for case, start in enumerate(origins):
            length = (30 << 25)+20000
            rows = []
            for target in [0, 64, 191, 1966000, 1966080, 1966170, (30 << 25)-90, (30 << 25)+90]:
                n = next(start+target+i for i in range(210) if (start+target+i) % 7 == 0 and math.gcd(start+target+i, 30) == 1)
                rows.append((n, 7, n//7))
            rows = sorted(set(rows))
            path = self.page('map-'+str(case), start, length, rows)
            lo, hi = words(start)
            # The harness uses whitespace-delimited paths; use a relative path
            # under its private cwd so Windows user-profile spaces are harmless.
            commands.append(f'PAGE {hi} {lo} {length} {path.name}')
            for n, _, _ in rows:
                origin = (n-90)//30*30
                lo, hi = words(origin)
                commands.append(f'MAP {hi} {lo} 64')
                candidates = [origin+30*(i//8)+CLASSES[i % 8] for i in range(64)]
                marked = [i for i, v in enumerate(candidates) if v in {r[0] for r in rows}]
                expected.append(dict(changed=len(marked), marked=marked))
        p = subprocess.run([MAPPER], cwd=self.root, input='\n'.join(commands)+'\n', encoding='utf-8', capture_output=True, timeout=10)
        self.assertEqual(p.returncode, 0, p.stderr)
        self.assertEqual([json.loads(x) for x in p.stdout.splitlines()], expected)


if __name__ == '__main__':
    unittest.main()
