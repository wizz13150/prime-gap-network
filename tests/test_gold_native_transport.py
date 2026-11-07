"""Exercise bounded transport against the real native evidence helper."""
from pathlib import Path
import copy
import json
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol')]
from horizon_gold_proof import build

HELPER = Path(sys.argv.pop(1)).resolve()
PRODUCER = Path(sys.argv.pop(1)).resolve()


class NativeTransportTests(unittest.TestCase):
    def small(self):
        return dict(
            work=dict(ticket='2' * 64, start='100', length='100'),
            computed=dict(ticket_id='2' * 64, start='100', end='200',
                          best=[dict(lower_prime='113', upper_prime='127', gap=14)]),
            lower_prime='113',
        )

    def test_native_evidence_matches_previous_direct_transport(self):
        request = self.small()
        previous = subprocess.run([str(HELPER)], input=json.dumps(request).encode(),
                                  capture_output=True, timeout=10, check=True)
        actual = build(HELPER, request['work'], request['computed'], request['lower_prime'])
        self.assertEqual(actual, json.loads(previous.stdout))
        self.assertIs(actual['verified'], True)
        self.assertIs(actual['network_accepted'], False)

    def test_native_82bit_scientific_evidence_is_conserved(self):
        with tempfile.TemporaryDirectory() as folder:
            certificate = Path(folder) / 'supplied-gap.pgncg'
            subprocess.run([str(PRODUCER), str(certificate)], capture_output=True,
                           timeout=10, check=True)
            # This helper produces the exact 82-bit test certificate used by
            # gap-proof-reuse. The ordinary builder has its own finite budget;
            # pass the certificate through the native input protocol directly.
            request = dict(
                work=dict(ticket='1' * 64, start='3317044064679887385953610', length='24576'),
                computed=dict(ticket_id='1' * 64, start='3317044064679887385953610',
                              end='3317044064679887385978186',
                              best=[dict(lower_prime='3317044064679887385961813',
                                         upper_prime='3317044064679887385962123', gap=310)]),
                lower_prime='3317044064679887385961813',
                supplied_scientific_bytes=certificate.read_bytes().hex(),
            )
            from horizon_bounded_child import run
            actual = run([str(HELPER)], json.dumps(request).encode())
            self.assertEqual(actual.returncode, 0)
            evidence = json.loads(actual.stdout)
            self.assertEqual(evidence['scientific_bytes'], request['supplied_scientific_bytes'])
            self.assertIs(evidence['network_accepted'], False)

    def test_native_bad_ticket_binding_still_fails(self):
        request = self.small()
        changed = copy.deepcopy(request['computed'])
        changed['ticket_id'] = '3' * 64
        with self.assertRaises(RuntimeError):
            build(HELPER, request['work'], changed, request['lower_prime'])


if __name__ == '__main__':
    unittest.main()
