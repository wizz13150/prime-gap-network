"""Private backup contracts: conservation, bounded input and no wallet leakage."""
import hashlib
import gzip
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch
import zipfile

PROJECT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location("scientific_backup", PROJECT / "tools/export_scientific_results.py")
backup = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(backup)
GENESIS = "7" * 64
SECRET = "THIS_MUST_NEVER_ENTER_A_SCIENTIFIC_EXPORT"


class ScientificBackupTests(unittest.TestCase):
    def named_report(self, name):
        from report_commitment import seal_report
        work = dict(start=self.cfg['start'], length=self.report['body']['positions'],
                    ticket=self.folder.name, genesis=GENESIS, request_id='b'*64,
                    work_id='c'*64, target_numerator=32, target_denominator=1)
        return seal_report(self.report, work, nonce='d'*64, discoverer=name)

    def test_current_producer_named_report_survives_offline_backup_exactly(self):
        network = self.named_report('Émile-31')
        self.write('network-report.json', network)
        before = self.source_hashes()
        self.assertTrue(self.create()['PASS'])
        self.assertEqual(self.source_hashes(), before)
        with zipfile.ZipFile(self.output) as archive:
            retained = json.loads(archive.read(f'tickets/{self.folder.name}/sealed-report.json'))
            self.assertEqual(retained, network)
            self.assertNotIn(SECRET.encode(), b''.join(archive.read(n) for n in archive.namelist()))
        self.assertTrue(backup.verify(self.output, GENESIS)['PASS'])

    def test_current_profile_name_is_accepted_as_private_backup_label(self):
        self.assertTrue(backup.create(self.output, [self.folder], GENESIS, 'Émile-31')['PASS'])
        self.assertTrue(backup.verify(self.output, GENESIS)['PASS'])

    def test_named_report_unknown_fields_are_rejected_even_with_recomputed_root(self):
        for extra_attribution in (True, False):
            network = self.named_report('PGN')
            target = network['body']['discoverer'] if extra_attribution else network['body']
            target['private_key'] = SECRET
            network['report_root'] = backup.digest(network['body'])
            self.write('network-report.json', network)
            with self.assertRaises(ValueError):
                self.create()
            self.assertFalse(self.output.exists())

    def test_named_report_name_cannot_change_without_changing_commitment(self):
        network = self.named_report('PGN')
        network['body']['discoverer']['name'] = 'Other'
        self.write('network-report.json', network)
        with self.assertRaises(ValueError):
            self.create()
        self.assertFalse(self.output.exists())

    def test_backup_name_uses_current_limits_and_preserves_legacy_labels(self):
        for name in ('', 'PGN+legacy', 'É'*48):
            self.assertEqual(backup._label(name), name)
        for name in ('É'*49, 'E\u0301mile', 'PGN\n', '\ud800', 12, None):
            with self.assertRaises((ValueError, UnicodeError)):
                backup._label(name)

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.folder = self.root / "tickets" / ("1" * 64)
        self.folder.mkdir(parents=True)
        start, end = 150000000000000000000, 150000200000000000000
        self.cfg = dict(ticket_id=self.folder.name, start=str(start), end=str(end), threshold_policy=dict(sha256="a" * 64), rpc_password=SECRET)
        body = dict(schema="PGN_HORIZON_COMPUTED_REPORT_V1", ticket_id=self.folder.name,
                    start=str(start), end=str(end), positions=str(end-start), policy_sha256="a" * 64,
                    counts=["3"] + ["0"] * 99,
                    best=[dict(gap=960, lower_prime=str(start+1), upper_prime=str(start+961))])
        self.report = dict(body=body, sha256=backup.digest(body), network_accepted=False)
        self.meta = dict(schema="PGN68_DURABLE_SESSION_V1", state="COMPLETE", current_chunk=None, configuration=self.cfg)
        self.write("session.json", self.meta)
        self.write("report.json", self.report)
        self.network = dict(body=dict(schema="PGN_NETWORK_RANGE_REPORT_V1", genesis=GENESIS,
                                     request_id="b"*64, ticket=self.folder.name, issue_context="c"*64,
                                     target_numerator=32, target_denominator=1,
                                     commitment_nonce="d"*64, computed=body), scientific_accepted=False)
        self.network["report_root"] = backup.digest(self.network["body"])
        self.write("network-report.json", self.network)
        (self.folder / "wallet.dat").write_text(SECRET)
        (self.folder / ".cookie").write_text(SECRET)
        (self.folder / "session-index.sqlite3").write_text(SECRET)
        self.output = self.root / "private-science.zip"

    def write(self, name, value):
        (self.folder / name).write_bytes(backup.canonical_bytes(value))

    def create(self):
        return backup.create(self.output, [self.folder], GENESIS, "PGN")

    def source_hashes(self):
        return {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in self.folder.iterdir() if p.is_file()}

    def test_export_verifies_offline_preserves_exact_results_and_no_secrets(self):
        before = self.source_hashes()
        result = self.create()
        self.assertTrue(result["PASS"])
        self.assertEqual(result["tickets"], 1)
        self.assertEqual(result["sealed_reports"], 1)
        self.assertFalse(result["network_acceptance_proven"])
        self.assertEqual(self.source_hashes(), before)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(len(archive.namelist()), 3)
            for name in archive.namelist():
                self.assertNotIn(SECRET.encode(), archive.read(name))
            exported = json.loads(archive.read(f"tickets/{self.folder.name}/computed.json"))
            self.assertEqual(exported, self.report)
        run = subprocess.run([sys.executable, "-I", "-X", "utf8", str(PROJECT/"tools/export_scientific_results.py"), "--verify", str(self.output), "--genesis", GENESIS], capture_output=True, text=True, timeout=15)
        self.assertEqual(run.returncode, 0, run.stderr)
        self.assertTrue(json.loads(run.stdout)["PASS"])

    def test_active_or_recoverable_work_is_never_called_complete(self):
        for state, chunk in (("RUNNING", None), ("COMPLETE", {"unfinished": True})):
            self.meta.update(state=state, current_chunk=chunk)
            self.write("session.json", self.meta)
            before = self.source_hashes()
            with self.assertRaises(ValueError):
                self.create()
            self.assertEqual(self.source_hashes(), before)
            self.assertFalse(self.output.exists())

    def test_no_clobber_existing_backup(self):
        self.create()
        before = self.output.read_bytes()
        with self.assertRaises(ValueError):
            self.create()
        self.assertEqual(self.output.read_bytes(), before)

    def test_foreign_genesis_and_changed_report_refused(self):
        with self.assertRaises(ValueError):
            backup.create(self.output, [self.folder], "8" * 64)
        self.report["body"]["counts"][0] = "4"
        self.write("report.json", self.report)
        with self.assertRaises(ValueError):
            self.create()
        self.assertFalse(self.output.exists())

    def test_unknown_payload_fields_fail_closed_instead_of_copying_secrets(self):
        self.report["body"]["private_key"] = SECRET
        self.report["sha256"] = backup.digest(self.report["body"])
        self.write("report.json", self.report)
        with self.assertRaises(ValueError):
            self.create()

    def test_sealed_root_and_computed_body_must_match(self):
        self.network["body"]["commitment_nonce"] = "e" * 64
        self.write("network-report.json", self.network)
        with self.assertRaises(ValueError):
            self.create()

    def test_duplicate_or_empty_selection_refused(self):
        for folders in ([], [self.folder, self.folder], [self.folder] * 513):
            with self.assertRaises(ValueError):
                backup.create(self.output, folders, GENESIS)
        self.assertFalse(self.output.exists())

    def test_decoded_budget_failure_leaves_no_partial_output(self):
        with patch.object(backup, "MAXIMUM_BYTES", 512 * 1024):
            with self.assertRaises(ValueError):
                self.create()
        self.assertFalse(self.output.exists())
        self.assertFalse(list(self.root.glob(".pgn-science-*")))

    def test_unsealed_standalone_results_remain_explicitly_unaccepted(self):
        (self.folder / "network-report.json").unlink()
        result = self.create()
        self.assertEqual(result["sealed_reports"], 0)
        self.assertFalse(result["network_acceptance_proven"])

    def test_actual_compact_archives_reuse_existing_validator(self):
        from horizon_ticket_archive import SCHEMA, ARCHIVE_NAME
        frame = dict(ticket_id=self.folder.name, start=self.cfg["start"], end=self.cfg["end"],
                     positions=self.report["body"]["positions"], phase="Report ready",
                     scopes=dict(ticket=dict(statistics=dict(count_total="3"))))
        body = dict(schema=SCHEMA, configuration=self.cfg,
                    configuration_sha256=backup.digest(self.cfg), report=self.report, frame=frame)
        encoded = gzip.compress(backup.canonical_bytes(body), mtime=0)
        (self.folder / ARCHIVE_NAME).write_bytes(encoded)
        self.write("session.json", dict(schema=SCHEMA, ticket_id=self.folder.name,
                                      archive=ARCHIVE_NAME, sha256=hashlib.sha256(encoded).hexdigest()))
        (self.folder / "report.json").unlink()
        before = self.source_hashes()
        self.assertTrue(self.create()["PASS"])
        self.assertEqual(self.source_hashes(), before)

    def test_corrupted_compact_archive_is_not_exported(self):
        from horizon_ticket_archive import SCHEMA, ARCHIVE_NAME
        (self.folder / ARCHIVE_NAME).write_bytes(b"wrong")
        self.write("session.json", dict(schema=SCHEMA, ticket_id=self.folder.name,
                                      archive=ARCHIVE_NAME, sha256="a" * 64))
        with self.assertRaises(ValueError):
            self.create()
        self.assertFalse(self.output.exists())

    def test_concurrent_publication_has_one_winner_and_no_overwrite(self):
        results, errors = [], []
        def run():
            try:
                results.append(self.create())
            except (ValueError, FileExistsError) as error:
                errors.append(type(error).__name__)
        threads = [threading.Thread(target=run) for _ in range(2)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join(10)
        self.assertEqual(len(results), 1)
        self.assertEqual(len(errors), 1)
        self.assertTrue(backup.verify(self.output, GENESIS)["PASS"])

    def test_verify_rejects_extra_archive_member_without_extracting(self):
        self.create()
        with zipfile.ZipFile(self.output, "a") as archive:
            archive.writestr("../../wallet.dat", SECRET)
        with self.assertRaises(ValueError):
            backup.verify(self.output, GENESIS)
        self.assertFalse((self.root.parent / "wallet.dat").exists())

    def test_private_objects_in_hash_and_merit_fields_are_rejected(self):
        import copy
        for key in ("request_id", "issue_context", "target_numerator", "target_denominator"):
            value = copy.deepcopy(self.network)
            value["body"][key] = {"private_key": SECRET}
            value["report_root"] = backup.digest(value["body"])
            self.write("network-report.json", value)
            with self.assertRaises(ValueError):
                self.create()
            self.assertFalse(self.output.exists())

    def test_canonical_native_merit_strings_are_preserved(self):
        self.network["body"].update(target_numerator="1831077", target_denominator="65536")
        self.network["report_root"] = backup.digest(self.network["body"])
        self.write("network-report.json", self.network)
        self.assertTrue(self.create()["PASS"])

    def test_invalid_merit_scalars_are_rejected(self):
        import copy
        for value in (True, 1.5, "01", "-1", "0", "4294967296", [32]):
            changed = copy.deepcopy(self.network)
            changed["body"]["target_numerator"] = value
            changed["report_root"] = backup.digest(changed["body"])
            self.write("network-report.json", changed)
            with self.assertRaises(ValueError):
                self.create()

    def test_verifier_rejects_private_extra_metadata_fields(self):
        self.create()
        with zipfile.ZipFile(self.output) as source:
            original = {name: source.read(name) for name in source.namelist()}
        for location in ("manifest", "attribution", "member"):
            manifest = json.loads(original["manifest.json"])
            target = manifest if location == "manifest" else manifest["attribution"] if location == "attribution" else manifest["members"][0]
            target["private_key"] = SECRET
            changed = dict(original, **{"manifest.json": backup.canonical_bytes(manifest)})
            archive_path = self.root / (location + ".zip")
            with zipfile.ZipFile(archive_path, "w") as archive:
                for name, data in changed.items():
                    archive.writestr(name, data)
            with self.assertRaises(ValueError):
                backup.verify(archive_path, GENESIS)

    def test_duplicate_json_keys_are_rejected_even_if_final_value_looks_valid(self):
        with self.assertRaises(ValueError):
            backup._decode(b'{"private_key":"secret","private_key":"overwritten"}')
        self.create()
        with zipfile.ZipFile(self.output) as source:
            original = {name: source.read(name) for name in source.namelist()}
        raw = original["manifest.json"].rstrip()
        original["manifest.json"] = b'{"schema":"duplicate",' + raw[1:]
        changed = self.root / "duplicate.zip"
        with zipfile.ZipFile(changed, "w") as archive:
            for name, data in original.items():
                archive.writestr(name, data)
        with self.assertRaises(ValueError):
            backup.verify(changed, GENESIS)


if __name__ == "__main__":
    unittest.main()
