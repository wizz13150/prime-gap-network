"""Conservation across shard boundaries, corruption, quotas and publication races."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(PROJECT / "tools"))
import scientific_backup_set as sets
single = sets.single
GENESIS = "7" * 64
SECRET = "NEVER_EXPORT_WALLET_OR_RPC_SECRET"


class ScientificBackupSetTests(unittest.TestCase):
    def test_committed_discoverer_names_survive_across_distinct_shards(self):
        from report_commitment import seal_report
        folders = [self.ticket(1), self.ticket(2)]
        names = ['Émile-31', 'PGN']
        originals = []
        for folder, name in zip(folders, names):
            report = json.loads((folder/'report.json').read_bytes())
            work = dict(start=report['body']['start'], length=report['body']['positions'],
                        ticket=folder.name, genesis=GENESIS, request_id='b'*64,
                        work_id='c'*64, target_numerator=32, target_denominator=1)
            sealed = seal_report(report, work, nonce='d'*64, discoverer=name)
            (folder/'network-report.json').write_bytes(single.canonical_bytes(sealed))
            originals.append(sealed)
        with patch.object(single, 'MAXIMUM_TICKETS', 1):
            result = sets.create_set(self.output, iter(folders), GENESIS, 'Émile-31')
            self.assertEqual((2, 2, 2), (result['tickets'], result['shards'], result['sealed_reports']))
            self.assertEqual(result['index_sha256'], sets.verify_set(self.output, GENESIS)['index_sha256'])
            retained = []
            for row in self.index()['shards']:
                with zipfile.ZipFile(self.output/row['path']) as archive:
                    for member in archive.namelist():
                        raw = archive.read(member)
                        self.assertNotIn(SECRET.encode(), raw)
                        if member.endswith('/sealed-report.json'):
                            retained.append(json.loads(raw))
        self.assertEqual(retained, originals)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.tickets = self.root / "tickets"
        self.tickets.mkdir()
        self.output = self.root / "backup"

    def ticket(self, number, parent=None, state="COMPLETE"):
        ticket = f"{number:064x}"
        folder = (parent or self.tickets) / ticket
        folder.mkdir(parents=True)
        start, end = 150000000000000000000, 150000200000000000000
        body = dict(schema="PGN_HORIZON_COMPUTED_REPORT_V1", ticket_id=ticket,
                    start=str(start), end=str(end), positions=str(end-start), policy_sha256="a" * 64,
                    counts=["3"] + ["0"] * 99,
                    best=[dict(gap=960, lower_prime=str(start+1), upper_prime=str(start+961))])
        report = dict(body=body, sha256=single.digest(body), network_accepted=False)
        marker = dict(schema="PGN68_DURABLE_SESSION_V1", state=state, current_chunk=None,
                      configuration=dict(ticket_id=ticket, start=str(start), end=str(end),
                                         threshold_policy=dict(sha256="a"*64), rpc_password=SECRET))
        (folder / "report.json").write_bytes(single.canonical_bytes(report))
        (folder / "session.json").write_bytes(single.canonical_bytes(marker))
        (folder / "wallet.dat").write_text(SECRET)
        return folder

    def create(self, folders=None, **kwargs):
        return sets.create_set(self.output, folders or sets.CompletedTickets(self.tickets), GENESIS, "PGN", **kwargs)

    def index(self):
        return json.loads((self.output / sets.INDEX).read_bytes())

    def change_index(self, change):
        index = self.index()
        change(index)
        (self.output / sets.INDEX).write_bytes(single.canonical_bytes(index))

    def test_1025_completed_tickets_are_all_conserved_across_three_shards(self):
        for number in range(1, 1026):
            self.ticket(number)
        self.ticket(1026, state="RUNNING")
        (self.tickets / "note.txt").write_text(SECRET)
        before = {str(path.relative_to(self.tickets)): hashlib.sha256(path.read_bytes()).hexdigest()
                  for path in self.tickets.rglob("*") if path.is_file()}
        result = self.create()
        self.assertEqual((1025, 3), (result["tickets"], result["shards"]))
        self.assertEqual(dict(mode="observed_completed_directory", active_excluded=1,
                              other_entries_excluded=1), result["selection"])
        expected = hashlib.sha256("".join(f"{n:064x}\n" for n in range(1, 1026)).encode()).hexdigest()
        self.assertEqual(expected, result["ticket_set_sha256"])
        self.assertEqual(result["index_sha256"], sets.verify_set(self.output, GENESIS)["index_sha256"])
        exported = set()
        for row in self.index()["shards"]:
            self.assertLessEqual(row["bytes"], single.MAXIMUM_BYTES)
            with zipfile.ZipFile(self.output / row["path"]) as archive:
                manifest = json.loads(archive.read("manifest.json"))
                self.assertTrue(manifest["contains_private_audit_answers"])
                exported.update(manifest["tickets"])
                for name in archive.namelist():
                    self.assertNotIn(SECRET.encode(), archive.read(name))
        self.assertEqual({f"{n:064x}" for n in range(1, 1026)}, exported)
        after = {str(path.relative_to(self.tickets)): hashlib.sha256(path.read_bytes()).hexdigest()
                 for path in self.tickets.rglob("*") if path.is_file()}
        self.assertEqual(before, after)
        self.assertFalse(any(path.name.startswith(".pgn-science-") for path in self.root.iterdir()))

    def test_duplicate_after_full_shard_refuses_entire_export(self):
        folders = [self.ticket(n) for n in range(1, 514)]
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            self.create(iter(folders + [folders[0]]))
        self.assertFalse(self.output.exists())

    def test_corrupt_completed_ticket_is_never_silently_skipped(self):
        self.ticket(1)
        broken = self.ticket(2)
        (broken / "report.json").write_text("{}")
        with self.assertRaises((ValueError, KeyError)):
            self.create()
        self.assertFalse(self.output.exists())

    def test_selected_active_unknown_state_and_missing_metadata_fail(self):
        folder = self.ticket(1, state="RUNNING")
        with self.assertRaises(ValueError):
            self.create([folder])
        for value in ("FUTURE_STATE", None):
            marker = json.loads((folder / "session.json").read_bytes())
            marker["state"] = value
            (folder / "session.json").write_bytes(single.canonical_bytes(marker))
            with self.assertRaises(ValueError):
                self.create()
        (folder / "session.json").unlink()
        with self.assertRaises(ValueError):
            self.create()
        self.assertFalse(self.output.exists())

    def test_storage_decoded_and_scratch_quotas_fail_without_output(self):
        folder = self.ticket(1)
        with self.assertRaises(ValueError):
            self.create(maximum_bytes=1)
        with patch.object(sets, "SCRATCH_BUDGET", 4096):
            with self.assertRaises(Exception):
                self.create([folder])
        self.assertFalse(self.output.exists())
        self.create([folder])
        with self.assertRaises(ValueError):
            sets.verify_set(self.output, maximum_bytes=1)

    def test_empty_or_existing_directory_never_overwritten(self):
        with self.assertRaises(ValueError):
            self.create(iter([]))
        self.ticket(1)
        self.output.mkdir()
        with self.assertRaises(ValueError):
            self.create()
        self.assertEqual([], list(self.output.iterdir()))

    def test_shard_quota_never_publishes_a_truncated_prefix(self):
        self.ticket(1)
        self.ticket(2)
        with patch.object(sets, "MAXIMUM_SHARDS", 1), patch.object(sets, "CHUNK_BYTES", 1):
            with self.assertRaisesRegex(ValueError, "shard quota"):
                self.create()
        self.assertFalse(self.output.exists())

    def test_index_change_during_verification_is_rejected(self):
        self.ticket(1)
        self.create()
        verify = sets._verified_shard
        def changed(*args):
            result = verify(*args)
            self.change_index(lambda m: m["attribution"].update(public_name="Changed"))
            return result
        with patch.object(sets, "_verified_shard", side_effect=changed):
            with self.assertRaises(ValueError):
                sets.verify_set(self.output)

    def test_two_concurrent_publishers_cannot_overwrite_each_other(self):
        folder = self.ticket(1)
        outcomes = []
        barrier = threading.Barrier(2)
        publish = sets._publish
        def synchronize(*args):
            barrier.wait(timeout=15)
            return publish(*args)
        def run():
            try:
                self.create([folder])
                outcomes.append("ok")
            except FileExistsError:
                outcomes.append("exists")
        with patch.object(sets, "_publish", side_effect=synchronize):
            threads = [threading.Thread(target=run) for _ in range(2)]
            for thread in threads:
                thread.start()
            for thread in threads:
                thread.join(timeout=25)
            self.assertFalse(any(thread.is_alive() for thread in threads))
        self.assertEqual(["exists", "ok"], sorted(outcomes))
        self.assertEqual(1, sets.verify_set(self.output)["tickets"])

    def test_interrupted_publication_removes_only_owned_links(self):
        folder = self.ticket(1)
        link = os.link
        def fail_index(source, target):
            if Path(target).name == sets.INDEX:
                raise OSError("Simulated interrupted commit")
            return link(source, target)
        with patch.object(sets.os, "link", side_effect=fail_index):
            with self.assertRaises(OSError):
                self.create([folder])
        self.assertFalse(self.output.exists())

    def test_missing_index_shard_extra_file_and_wrong_genesis_refuse(self):
        self.ticket(1)
        self.create()
        with self.assertRaises(ValueError):
            sets.verify_set(self.output, "8"*64)
        unexpected = self.output / "unexpected"
        unexpected.write_text("x")
        with self.assertRaises(ValueError):
            sets.verify_set(self.output)
        unexpected.unlink()
        shard = self.output / "part-000001.zip"
        raw = shard.read_bytes()
        shard.unlink()
        with self.assertRaises(ValueError):
            sets.verify_set(self.output)
        shard.write_bytes(raw)
        (self.output / sets.INDEX).unlink()
        with self.assertRaises((ValueError, OSError)):
            sets.verify_set(self.output)

    def test_corruption_cannot_be_hidden_by_rehashing_index(self):
        self.ticket(1)
        self.create()
        shard = self.output / "part-000001.zip"
        raw = bytearray(shard.read_bytes())
        raw[len(raw)//2] ^= 1
        shard.write_bytes(raw)
        self.change_index(lambda m: m["shards"][0].update(sha256=sets._sha(shard)))
        with self.assertRaises(Exception):
            sets.verify_set(self.output)

    def test_cross_shard_duplicate_is_rejected_even_with_correct_shard_hashes(self):
        self.ticket(1)
        self.create()
        shutil.copyfile(self.output / "part-000001.zip", self.output / "part-000002.zip")
        def duplicate(index):
            row = dict(index["shards"][0], path="part-000002.zip")
            index["shards"].append(row)
            index["tickets"] *= 2
            index["decoded_bytes"] *= 2
        self.change_index(duplicate)
        with self.assertRaisesRegex(ValueError, "Duplicate"):
            sets.verify_set(self.output)

    def test_invalid_index_fields_types_flags_digest_and_paths_refuse(self):
        self.ticket(1)
        self.create()
        original = (self.output / sets.INDEX).read_bytes()
        mutations = [lambda m: m.update(private_backup=False),
                     lambda m: m.update(contains_private_audit_answers=False),
                     lambda m: m.update(network_acceptance_proven=True),
                     lambda m: m.update(reset_or_token_conversion_authorized=True),
                     lambda m: m.update(tickets=True),
                     lambda m: m.update(ticket_set_sha256="0"*64),
                     lambda m: m.update(undocumented="x"),
                     lambda m: m["shards"][0].update(path="../part-000001.zip"),
                     lambda m: m["shards"][0].update(tickets=True),
                     lambda m: m["attribution"].update(public_name="Different"),
                     lambda m: m["selection"].update(mode="network_atomic_snapshot")]
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                (self.output / sets.INDEX).write_bytes(original)
                self.change_index(mutation)
                with self.assertRaises(ValueError):
                    sets.verify_set(self.output)
        (self.output / sets.INDEX).write_bytes(original[:-2] + b',"tickets":1}\n')
        with self.assertRaises(ValueError):
            sets.verify_set(self.output)

    def test_cli_directory_backup_and_verification(self):
        self.ticket(1)
        command = [sys.executable, "-I", "-X", "utf8", str(PROJECT / "tools/export_scientific_results.py"),
                   "--genesis", GENESIS]
        created = subprocess.run(command + ["--output-directory", str(self.output), "--tickets-root", str(self.tickets)],
                                 check=True, capture_output=True, text=True, timeout=20)
        checked = subprocess.run(command + ["--verify-directory", str(self.output)], check=True,
                                 capture_output=True, text=True, timeout=20)
        self.assertEqual(1, json.loads(created.stdout)["tickets"])
        self.assertTrue(json.loads(checked.stdout)["PASS"])

    def test_redirected_directory_and_shard_are_rejected(self):
        self.ticket(1)
        self.create()
        redirect = self.root / "redirect"
        if os.name == "nt":
            subprocess.run(["cmd", "/c", "mklink", "/J", str(redirect), str(self.output)],
                           check=True, capture_output=True, timeout=10)
            self.addCleanup(redirect.rmdir)
        else:
            redirect.symlink_to(self.output, target_is_directory=True)
        with self.assertRaises(ValueError):
            sets.verify_set(redirect)


if __name__ == "__main__":
    unittest.main()
