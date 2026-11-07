"""Calculated work must enter publication recovery before any next GPU job."""
from pathlib import Path
from types import SimpleNamespace
import json
import sys
import tempfile
import threading
import unittest
from unittest.mock import Mock

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / "src/engine/runtime_host"), str(ROOT / "src/protocol")]
from horizon_mining_flow import MiningFlow
from horizon_ticket_archive import SCHEMA


class Promoted(Exception):
    pass


class CompletedRecoveryTests(unittest.TestCase):
    def check(self, saved, completed):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            work = dict(ticket="1" * 64, request_id="2" * 64,
                        start="150000000000000000000", length="200000000000000",
                        target_numerator=32, target_denominator=1)
            following = dict(work, ticket="3" * 64)
            folder = root / "tickets" / work["ticket"]
            folder.mkdir(parents=True)
            (folder / "session.json").write_text(json.dumps(saved))
            source = Mock()
            report = {"retained": "exact calculated result"}
            source.run_ticket.return_value = report
            worker = SimpleNamespace(root=root, network_lock=threading.RLock(),
                                     assign=Mock(return_value=work), source=source,
                                     args=SimpleNamespace(piece_limit=16 * 10**12))
            flow = MiningFlow.__new__(MiningFlow)
            flow.worker = worker
            flow.allocator = Mock()
            flow.allocator.assign.return_value = following
            flow.outbox = Mock()
            # This regression exercises pre-cycle legacy sessions. Supply the
            # current constructor contract while leaving recovery assertions intact.
            flow.cycles = Mock()
            flow.cycles.contains.return_value = False
            flow.view_sequence = 0
            flow.promote = Mock(side_effect=Promoted)
            with self.assertRaises(Promoted):
                flow.run()
            if completed:
                source.run_ticket.assert_called_once()
                flow.outbox.put.assert_called_once_with(work, report)
                flow.allocator.assign.assert_called_once_with()
                flow.promote.assert_called_once_with(following, 0)
            else:
                source.run_ticket.assert_not_called()
                flow.outbox.put.assert_not_called()
                flow.allocator.assign.assert_not_called()
                flow.promote.assert_called_once_with(work, 0)

    def test_original_completed_session(self):
        self.check({"state": "COMPLETE"}, True)

    def test_compact_completed_session(self):
        self.check({"schema": SCHEMA}, True)

    def test_paused_work_is_not_claimed_complete(self):
        self.check({"state": "PAUSED"}, False)


if __name__ == "__main__":
    unittest.main()
