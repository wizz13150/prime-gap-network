"""Crash/ACK/retention contracts of the native output transport; no GPU."""
import importlib.util
import json
from pathlib import Path
import tempfile
import threading
import unittest
import sys

SOURCE = Path(__file__).resolve().parents[1] / "src/engine/runtime_host/pgn_output_spool.py"
sys.path.insert(0, str(SOURCE.parent))
SPEC = importlib.util.spec_from_file_location("output_spool", SOURCE)
spool = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(spool)
TOKEN = "a" * 32


class SpoolContracts(unittest.TestCase):
    def test_done_observation_rereads_last_tail(self):
        writer = self.writer()
        done = False
        def finish_between_EOF_and_done():
            nonlocal done
            if not done:
                writer.append(b"late\n")
                writer.finish()
                done = True
            return done
        self.assertEqual(list(spool.Reader(self.root, finish_between_EOF_and_done)), ["late\n"])

    def test_done_observation_rechecks_rotated_next_segment(self):
        writer = self.writer()
        expected = self.lines(3)
        def finish_between_EOF_and_done():
            if not writer.closed:
                for line in expected:
                    writer.append(line)
                writer.finish()
            return True
        self.assertEqual([line.encode() for line in spool.Reader(self.root, finish_between_EOF_and_done)], expected)

    def test_public_future_ack_rejected_without_deletion(self):
        writer = self.writer()
        for line in self.lines(4):
            writer.append(line)
        writer.finish()
        with self.assertRaises(spool.SpoolError):
            spool.acknowledge(self.root, TOKEN, 10**9)
        writer.collect()
        self.assertEqual(sum(p.stat().st_size for p in self.root.glob("*.log")), 128)

    def test_reader_ack_cannot_advance_past_delivered_position(self):
        writer = self.writer()
        for line in self.lines(4):
            writer.append(line)
        writer.finish()
        reader = spool.Reader(self.root, lambda: True)
        next(reader)
        with self.assertRaises(spool.SpoolError):
            reader.acknowledge(TOKEN, 64)
        reader.close()
        self.assertFalse((self.root / "ack.json").exists())

    def test_collect_after_finish_preserves_finished_metadata(self):
        writer = self.writer()
        for line in self.lines(4):
            writer.append(line)
        writer.finish()
        spool.acknowledge(self.root, TOKEN, 64)
        writer.collect()
        seal = spool._json(self.root / "index.json")
        self.assertTrue(seal["finished"])
        spool.verified_segments(self.root, seal, TOKEN)

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "output"

    def writer(self, segment=64, maximum=256):
        writer = spool.Writer(self.root, TOKEN, segment, maximum)
        self.addCleanup(lambda: writer.finish() if not writer.closed else None)
        return writer

    def lines(self, count):
        return [(f"record-{i:05d} " + "x" * 18 + "\n").encode() for i in range(count)]

    def test_closed_stream_order_and_exact_logical_offsets(self):
        writer = self.writer()
        expected = self.lines(6)
        for line in expected:
            writer.append(line)
        seal = writer.finish()
        reader = spool.Reader(self.root, lambda: True)
        actual = list(reader)
        self.assertEqual([x.encode() for x in actual], expected)
        self.assertEqual([x.end_offset for x in actual], [sum(map(len, expected[:i])) for i in range(1, 7)])
        self.assertEqual(b"".join(x.read_bytes() for x in spool.verified_segments(self.root, seal, TOKEN)), b"".join(expected))

    def test_reading_all_bytes_does_not_authorize_deletion(self):
        writer = self.writer()
        for line in self.lines(6):
            writer.append(line)
        seal = writer.finish()
        paths = spool.verified_segments(self.root, seal, TOKEN)
        list(spool.Reader(self.root, lambda: True))
        writer.collect()
        self.assertTrue(all(p.exists() for p in paths))

    def test_explicit_durable_ack_retires_only_whole_consumed_segments(self):
        writer = self.writer()
        lines = self.lines(6)
        for line in lines:
            writer.append(line)
        writer.finish()
        self.assertTrue(spool.acknowledge(self.root, TOKEN, len(lines[0]) * 3))
        writer.collect()
        seal = writer._document(True)
        self.assertEqual(seal["base_offset"], len(lines[0]) * 2)
        self.assertFalse((self.root / spool._name(0)).exists())
        self.assertEqual(b"".join(p.read_bytes() for p in spool.verified_segments(self.root, seal, TOKEN)), b"".join(lines[2:]))

    def test_future_forged_boundary_cannot_retire_uncommitted_output(self):
        writer = self.writer()
        for line in self.lines(4):
            writer.append(line)
        before = sorted(self.root.glob("*.log"))
        spool._atomic(self.root / "ack.json", dict(schema=spool.SCHEMA, token=TOKEN, end_offset=999, index=0, sha256="f" * 64, link="f" * 64))
        with self.assertRaises(spool.SpoolError):
            writer.collect()
        self.assertEqual(sorted(self.root.glob("*.log")), before)
        (self.root / "ack.json").unlink()

    def test_other_owner_ack_is_rejected(self):
        writer = self.writer()
        writer.append(b"hello\n")
        spool._atomic(self.root / "ack.json", dict(schema=spool.SCHEMA, token="b" * 32, end_offset=0))
        with self.assertRaises(spool.SpoolError):
            writer.collect()
        (self.root / "ack.json").unlink()

    def test_backpressure_preserves_all_unacknowledged_data(self):
        writer = self.writer(maximum=128)
        accepted = []
        for line in self.lines(5):
            try:
                writer.append(line)
                accepted.append(line)
            except spool.SpoolError:
                break
        self.assertEqual(len(accepted), 4)
        seal = writer.finish()
        self.assertLessEqual(sum(p.stat().st_size for p in self.root.glob("*.log")), 128)
        self.assertEqual(b"".join(p.read_bytes() for p in spool.verified_segments(self.root, seal, TOKEN)), b"".join(accepted))

    def test_tampering_any_segment_rejects_entire_recovery_snapshot(self):
        writer = self.writer()
        for line in self.lines(6):
            writer.append(line)
        seal = writer.finish()
        last = self.root / spool._name(seal["segments"][-1]["index"])
        last.write_bytes(b"!" + last.read_bytes()[1:])
        with self.assertRaises(spool.SpoolError):
            spool.verified_segments(self.root, seal, TOKEN)

    def test_closed_snapshot_rejects_gap_duplicate_and_wrong_frontier(self):
        writer = self.writer()
        for line in self.lines(4):
            writer.append(line)
        seal = writer.finish()
        for mutate in (lambda x: x["segments"].reverse(),
                       lambda x: x["segments"].append(x["segments"][-1]),
                       lambda x: x.update(end_offset=x["end_offset"] + 1),
                       lambda x: x.update(finished=False)):
            candidate = json.loads(json.dumps(seal))
            mutate(candidate)
            with self.assertRaises(spool.SpoolError):
                spool.verified_segments(self.root, candidate, TOKEN)

    def test_many_acknowledged_jobs_remain_bounded(self):
        writer = self.writer(maximum=192)
        total = 0
        for i in range(500):
            line = self.lines(1)[0]
            writer.append(line)
            total += len(line)
            spool.acknowledge(self.root, TOKEN, total)
            writer.collect()
            self.assertLessEqual(sum(p.stat().st_size for p in self.root.glob("*.log")), 192)
            self.assertLessEqual(max(p.stat().st_size for p in self.root.glob("*.log")), 64)
        seal = writer.finish()
        self.assertEqual(seal["end_offset"], total)
        self.assertGreater(seal["base_offset"], 14000)
        spool.verified_segments(self.root, seal, TOKEN)

    def test_owner_ends_with_partial_line_is_not_success(self):
        self.root.mkdir()
        (self.root / spool._name(0)).write_bytes(b"partial")
        reader = spool.Reader(self.root, lambda: True)
        with self.assertRaises(spool.SpoolError):
            list(reader)
        reader.close()

    def test_repeated_ack_and_crash_between_index_publish_and_unlink(self):
        writer = self.writer()
        for line in self.lines(6):
            writer.append(line)
        writer.finish()
        spool.acknowledge(self.root, TOKEN, 64)
        old_reap = writer._reap
        writer._reap = lambda: None
        writer.collect()
        seal = spool._json(self.root / "index.json")
        seal["finished"] = True
        self.assertTrue((self.root / spool._name(0)).exists())
        self.assertEqual(len(spool.verified_segments(self.root, seal, TOKEN)), 2)
        writer.collect()
        writer._reap = old_reap
        writer.collect()
        self.assertFalse((self.root / spool._name(0)).exists())

    def test_live_reader_rotation_and_ack_during_writing(self):
        writer = self.writer(maximum=256)
        done = threading.Event()
        reader = spool.Reader(self.root, done.is_set)
        result, errors = [], []
        expected = self.lines(30)

        def receive():
            try:
                for line in reader:
                    result.append(line.encode())
                    # This fixture's list models an already durable consumer.
                    spool.acknowledge(self.root, TOKEN, line.end_offset)
            except BaseException as error:
                errors.append(error)

        thread = threading.Thread(target=receive)
        thread.start()
        try:
            import time
            for line in expected:
                writer.append(line)
                deadline = time.monotonic() + 2
                while len(result) < len(expected[:expected.index(line) + 1]) and not errors:
                    if time.monotonic() > deadline:
                        self.fail("Reader did not receive published output")
                    time.sleep(.001)
                writer.collect()
            writer.finish()
        finally:
            done.set()
            thread.join(3)
            reader.close()
        self.assertFalse(thread.is_alive())
        self.assertEqual(errors, [])
        self.assertEqual(result, expected)


if __name__ == "__main__":
    unittest.main()
