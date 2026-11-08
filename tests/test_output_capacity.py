"""Real filesystem reservations and concurrent processes; no GPU/native worker."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1]/'src/engine/runtime_host'
sys.path.insert(0, str(SOURCE))
import pgn_output_capacity as capacity
import pgn_output_spool as spool


class OutputCapacityTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.writers = []
        self.limit = patch.object(capacity, 'POOL_CAPACITY', 2*capacity.OWNER_CAPACITY)
        self.limit.start()
        self.addCleanup(self.limit.stop)
        self.addCleanup(self.close_writers)

    def close_writers(self):
        for writer in self.writers:
            if not writer.stream.closed:
                writer.finish()

    def owner(self, index):
        token = f'{index:032x}'
        owner = self.root/'tickets'/f'{index:064x}'/('native-owner-'+token)
        owner.mkdir(parents=True)
        return owner/'output', token

    def writer(self, index):
        output, token = self.owner(index)
        writer = spool.Writer(output, token, 64, 128)
        self.writers.append(writer)
        return writer

    def test_full_pool_rejects_before_output_creation_preserves_bytes(self):
        first, second = self.writer(1), self.writer(2)
        first.append(b'first\n'); second.append(b'second\n')
        before = {str(p):p.read_bytes() for p in self.root.rglob('*.log')}
        output, token = self.owner(3)
        with self.assertRaises(capacity.OutputCapacityExceeded):
            spool.Writer(output, token, 64, 128)
        self.assertFalse(output.exists())
        self.assertEqual({str(p):p.read_bytes() for p in self.root.rglob('*.log')}, before)

    def test_closed_small_owners_preserve_logs_without_reserving_future_growth(self):
        for index in range(1,18):
            writer=self.writer(index);writer.append(b'closed result\n');writer.finish()
        before={str(p):p.read_bytes() for p in self.root.rglob('*.log')}
        self.assertEqual(capacity._reserved_bytes(self.root/'tickets'),17*len(b'closed result\n'))
        active=self.writer(18)
        self.assertEqual(capacity._reserved_bytes(self.root/'tickets'),capacity.OWNER_CAPACITY+17*len(b'closed result\n'))
        self.assertEqual({str(p):p.read_bytes()for p in self.root.rglob('*.log')if str(p)in before},before)
        with self.assertRaises((ValueError,RuntimeError,OSError)):
            self.writers[0].append(b'cannot grow a sealed spool\n')

    def test_finish_and_read_without_ack_do_not_release_capacity(self):
        first = self.writer(1); self.writer(2)
        first.append(b'retained\n');first.finish()
        self.assertEqual(list(spool.Reader(first.root,lambda:True)), ['retained\n'])
        output,token=self.owner(3)
        with self.assertRaises(capacity.OutputCapacityExceeded):spool.Writer(output,token,64,128)

    def test_existing_durable_ack_releases_only_deleted_sealed_output(self):
        first = self.writer(1); self.writer(2)
        first.append(b'committed\n');first.finish()
        spool.acknowledge(first.root,first.token,len(b'committed\n'))
        first.collect()
        self.assertFalse(list(first.root.glob('*.log')))
        third = self.writer(3);third.append(b'new\n')
        self.assertEqual(capacity._reserved_bytes(self.root/'tickets'),2*capacity.OWNER_CAPACITY)
        self.assertTrue((first.root/'index.json').exists())

    def test_partial_constructor_directory_retains_crash_reservation(self):
        output,unused = self.owner(1);output.mkdir()
        self.writer(2)
        output,token = self.owner(3)
        with self.assertRaises(capacity.OutputCapacityExceeded):spool.Writer(output,token,64,128)

    def test_missing_or_corrupt_metadata_never_discounts_a_live_owner(self):
        first = self.writer(1)
        (first.root/'index.json').write_bytes(b'{')
        output,token = self.owner(2)
        with self.assertRaises(ValueError):spool.Writer(output,token,64,128)
        self.assertFalse(output.exists())

    def test_active_empty_file_remains_reserved(self):
        self.writer(1);self.writer(2)
        output,token=self.owner(3)
        with self.assertRaises(capacity.OutputCapacityExceeded):spool.Writer(output,token,64,128)

    def test_unlinked_permission_failure_does_not_free_capacity(self):
        first = self.writer(1);self.writer(2)
        first.append(b'held\n');first.finish()
        # A correct ACK exists, but a remaining log is still charged. This is
        # the on-disk state after a Windows sharing failure during retirement.
        spool.acknowledge(first.root,first.token,len(b'held\n'))
        output,token=self.owner(3)
        with self.assertRaises(capacity.OutputCapacityExceeded):spool.Writer(output,token,64,128)

    def test_inventory_has_a_finite_entry_budget(self):
        self.writer(1)
        output,token=self.owner(2)
        with patch.object(capacity,'MAXIMUM_ENTRIES',1):
            with self.assertRaises(capacity.OutputCapacityExceeded):spool.Writer(output,token,64,128)

    def test_existing_oversized_owner_is_not_undercharged(self):
        first=self.writer(1)
        first.append(b'a'*63+b'\n');first.append(b'b\n')
        output,token=self.owner(2)
        with patch.object(capacity,'OWNER_CAPACITY',64):
            with self.assertRaises(capacity.OutputCapacityExceeded):spool.Writer(output,token,64,128)

    def test_pool_and_writer_share_the_same_per_owner_limit(self):
        self.assertEqual(spool.MAXIMUM_BYTES,capacity.OWNER_CAPACITY)

    def test_noncanonical_lowlevel_spool_remains_supported(self):
        writer=spool.Writer(self.root/'standalone','a'*32,64,128)
        self.writers.append(writer);writer.append(b'fixture\n');writer.finish()
        self.assertEqual(list(spool.Reader(writer.root,lambda:True)),['fixture\n'])

    def test_no_capacity_inventory_on_append_or_resident_rotation(self):
        writer=self.writer(1)
        with patch.object(capacity,'_reserved_bytes',side_effect=AssertionError('Hot-path scan')):
            writer.append(b'a'*63+b'\n');writer.append(b'b\n');writer.finish()

    def test_redirection_never_expands_inventory_scope(self):
        output,token=self.owner(1)
        with patch.object(capacity,'_plain',side_effect=ValueError('redirected')):
            with self.assertRaisesRegex(ValueError,'redirected'):spool.Writer(output,token,64,128)
        self.assertFalse(output.exists())

    def test_three_real_processes_cannot_oversubscribe_two_slots(self):
        requests=[self.owner(i) for i in (1,2,3)]
        barrier=self.root/'go'
        code='''import sys,time
from pathlib import Path
sys.path.insert(0,sys.argv[1])
import pgn_output_capacity as c
from pgn_output_spool import Writer
c.POOL_CAPACITY=2*c.OWNER_CAPACITY
output,token,barrier=sys.argv[2:]
while not Path(barrier).exists():time.sleep(.005)
deadline=time.monotonic()+4
while True:
 try:
  w=Writer(output,token,64,128);w.append(b"retained\\n")
  Path(barrier).with_name('claim-'+token).touch()
  while len(list(Path(barrier).parent.glob('claim-*')))<3:
   if time.monotonic()>deadline:raise RuntimeError('Admission barrier timed out')
   time.sleep(.005)
  w.finish();sys.exit(0)
 except c.OutputCapacityBusy:
  if time.monotonic()>deadline:raise
  time.sleep(.01)
 except c.OutputCapacityExceeded:
  Path(barrier).with_name('claim-'+token).touch();sys.exit(3)
'''
        children=[]
        try:
            for output,token in requests:
                children.append(subprocess.Popen([sys.executable,'-B','-I','-c',code,str(SOURCE),str(output),token,str(barrier)],
                                                 stdout=subprocess.PIPE,stderr=subprocess.PIPE,
                                                 creationflags=0x08000000 if os.name=='nt' else 0))
            barrier.touch()
            codes=[]
            for child in children:
                stdout,stderr=child.communicate(timeout=7)
                self.assertIn(child.returncode,(0,3),stderr.decode(errors='replace'))
                codes.append(child.returncode)
            self.assertEqual(sorted(codes),[0,0,3])
            self.assertEqual(capacity._reserved_bytes(self.root/'tickets'),2*len(b'retained\n'))
        finally:
            for child in children:
                if child.poll() is None:child.kill()
                child.communicate()


if __name__=='__main__':unittest.main()
