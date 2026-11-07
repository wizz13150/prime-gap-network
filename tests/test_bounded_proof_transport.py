"""Real isolated pipe/timeout failures; fixture messages are not scientific proofs."""
from pathlib import Path
import json
import subprocess
import sys
import threading
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import horizon_bounded_child as child
import horizon_gold_proof as gold


class BoundedProofTests(unittest.TestCase):
    def command(self,code):
        return [sys.executable,'-B','-I','-c',code]

    def test_exact_response_and_input_are_conserved(self):
        raw=b'{"fixture":"public-only"}'
        result=child.run(self.command('import sys; data=sys.stdin.buffer.read();sys.stdout.buffer.write(data);sys.stderr.buffer.write(b"diagnostic")'),raw)
        self.assertEqual(result.returncode,0)
        self.assertEqual(result.stdout,raw)
        self.assertEqual(result.stderr,b'diagnostic')

    def test_legal_exact_output_bounds(self):
        result=child.run(self.command('import os;os.write(1,b"x"*16384);os.write(2,b"y"*1024)'),b'',stdout_limit=16384,stderr_limit=1024)
        self.assertEqual((len(result.stdout),len(result.stderr)),(16384,1024))

    def test_stdout_flood_stopped_during_read(self):
        with self.assertRaisesRegex(ValueError,'output exceeds budget'):
            child.run(self.command('import os;\nwhile True:os.write(1,b"x"*65536)'),b'',stdout_limit=16384)

    def test_stderr_flood_stopped_during_read(self):
        with self.assertRaisesRegex(ValueError,'output exceeds budget'):
            child.run(self.command('import os;\nwhile True:os.write(2,b"x"*65536)'),b'',stderr_limit=1024)

    def test_blocked_input_and_silent_child_are_timed_out(self):
        started=time.monotonic()
        with self.assertRaises(subprocess.TimeoutExpired):
            child.run(self.command('import time;time.sleep(10)'),b'x'*(1024*1024),timeout=.15)
        self.assertLess(time.monotonic()-started,3)

    def test_nonzero_exit_is_returned_without_broken_pipe_masking(self):
        result=child.run(self.command('import sys;sys.stdout.write("failed");sys.exit(7)'),b'x'*(1024*1024))
        self.assertEqual((result.returncode,result.stdout),(7,b'failed'))

    def test_transfer_budget_refuses_before_launch(self):
        for request,kwargs in [(b'x'*(1024*1024+1),{}),('text',{}),(b'',{'timeout':float('inf')}),
                               (b'',{'timeout':float('nan')}),(b'',{'stdout_limit':1.5}),
                               (b'',{'stderr_limit':16385}),(b'',{'timeout':0})]:
            with patch.object(child.subprocess,'Popen',side_effect=AssertionError('Launch forbidden')):
                with self.assertRaises(ValueError):child.run(['forbidden'],request,**kwargs)

    def test_failures_leave_no_transport_threads(self):
        before={thread.ident for thread in threading.enumerate()}
        for unused in range(4):
            with self.assertRaises(ValueError):
                child.run(self.command('import os;os.write(1,b"x"*32768)'),b'',stdout_limit=1024)
        self.assertEqual({thread.ident for thread in threading.enumerate()},before)

    def test_gold_assembly_contract_unchanged_for_legal_reply(self):
        expected={'verified':True,'network_accepted':False,'fixture':'not a proof'}
        with patch.object(gold,'bounded_child',return_value=subprocess.CompletedProcess([],0,json.dumps(expected).encode(),b'')) as transport:
            self.assertEqual(gold.build(Path(sys.executable),{'public_work':True},{'public_counts':[]},123),expected)
            request=json.loads(transport.call_args.args[1]);self.assertEqual(request['lower_prime'],'123')

    def test_native_error_and_malformed_json_cannot_become_evidence(self):
        for code,raw in [(1,b''),(0,b'not-json'),(0,b'[]'),(0,b'{"verified":false}')]:
            with patch.object(gold,'bounded_child',return_value=subprocess.CompletedProcess([],code,raw,b'')):
                with self.assertRaises((ValueError,RuntimeError)):
                    gold.build(Path(sys.executable),{}, {},123)


if __name__=='__main__':unittest.main()
