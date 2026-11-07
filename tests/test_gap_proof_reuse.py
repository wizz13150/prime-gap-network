"""Actual private helper process: untrusted supplied proof, exact pair binding."""
from pathlib import Path
import copy
import hashlib
import json
import subprocess
import sys
import tempfile
import unittest

HELPER = Path(sys.argv.pop(1)).resolve()
PRODUCER = Path(sys.argv.pop(1)).resolve()


class ProofReuseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temp = tempfile.TemporaryDirectory()
        cls.addClassCleanup(cls.temp.cleanup)
        proof = Path(cls.temp.name) / 'supplied-gap.pgncg'
        made = subprocess.run([str(PRODUCER), str(proof)], capture_output=True, timeout=10)
        if made.returncode:
            raise AssertionError(made.stderr.decode(errors='replace'))
        cls.proof = proof.read_bytes()
        assert hashlib.sha256(cls.proof).hexdigest() == '85c4f8a4c24706460f3b3468aedd600c20ff2a9889a51e4161f08b3b04a8b140'
        cls.request = dict(
            work=dict(ticket='1' * 64, start='3317044064679887385953610', length='24576'),
            computed=dict(ticket_id='1' * 64, start='3317044064679887385953610',
                          end='3317044064679887385978186',
                          best=[dict(lower_prime='3317044064679887385961813',
                                     upper_prime='3317044064679887385962123', gap=310)]),
            lower_prime='3317044064679887385961813',
            supplied_scientific_bytes=cls.proof.hex(),
        )

    def call(self, request, success):
        raw = request if isinstance(request, bytes) else json.dumps(request, separators=(',', ':')).encode()
        result = subprocess.run([str(HELPER)], input=raw, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0 if success else 1, result.stderr.decode(errors='replace'))
        value = json.loads(result.stdout)
        self.assertEqual(value['verified'], success)
        self.assertIs(value['network_accepted'], False)
        return value

    def test_exact_82bit_proof_and_range_commitment(self):
        value = self.call(self.request, True)
        self.assertEqual(value['scientific_bytes'], self.proof.hex())
        self.assertEqual(value['lower_prime'], self.request['lower_prime'])
        self.assertEqual(len(value['details_root']), 64)
        self.assertEqual(len(value['siblings']), 7)

    def test_ordinary_nohint_production_path_and_cached_output_agree(self):
        small = dict(work=dict(ticket='2' * 64, start='100', length='100'),
                     computed=dict(ticket_id='2' * 64, start='100', end='200',
                                   best=[dict(lower_prime='113', upper_prime='127', gap=14)]),
                     lower_prime='113')
        initial = self.call(small, True)
        small['supplied_scientific_bytes'] = initial['scientific_bytes']
        self.assertEqual(self.call(small, True), initial)

    def test_noncanonical_or_truncated_proof_never_falls_back(self):
        for supplied in ['', True, self.proof.hex()[:-1], self.proof.hex().upper(), self.proof[:-1].hex()]:
            with self.subTest(supplied_type=type(supplied).__name__):
                bad = copy.deepcopy(self.request)
                bad['supplied_scientific_bytes'] = supplied
                self.call(bad, False)

    def test_false_interior_witness_is_not_a_proof(self):
        self.assertEqual(self.proof[-1], 2)
        bad = copy.deepcopy(self.request)
        bad['supplied_scientific_bytes'] = (self.proof[:-1] + b'\x01').hex()
        self.call(bad, False)

    def test_valid_certificate_cannot_move_to_another_pair(self):
        bad = copy.deepcopy(self.request)
        for key in ('lower_prime', 'upper_prime'):
            bad['computed']['best'][0][key] = str(int(bad['computed']['best'][0][key]) + 2)
        bad['lower_prime'] = bad['computed']['best'][0]['lower_prime']
        self.call(bad, False)

    def test_ticket_binding_remains_required(self):
        bad = copy.deepcopy(self.request)
        bad['computed']['ticket_id'] = '3' * 64
        self.call(bad, False)

    def test_selected_record_remains_required(self):
        bad = copy.deepcopy(self.request)
        bad['computed']['best'] = []
        self.call(bad, False)

    def test_request_byte_budget_precedes_decoding(self):
        self.call(b' ' * (1024 * 1024 + 1), False)


if __name__ == '__main__':
    unittest.main()
