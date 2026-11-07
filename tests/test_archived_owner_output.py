"""Actual spool/archive formats, cross-ticket conservation and crash recovery; no GPU."""
from pathlib import Path
import gzip
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol')]
import horizon_ticket_archive as archive
import pgn_output_spool as spool

TOKEN = 'a' * 32


def save(path, body):
    path.write_bytes(archive.canonical(body))


class ArchivedOwnerOutputTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.tickets = self.root / 'tickets'
        self.tickets.mkdir()
        self.one, self.two = self.ticket(1), self.ticket(2)
        self.owner = self.one / ('native-owner-' + TOKEN)
        self.owner.mkdir()
        writer = spool.Writer(self.owner / 'output', TOKEN, 64, 1024)
        self.bindings = []
        for sequence, ticket in enumerate([self.one, self.two]):
            cfg = archive.load(ticket)['configuration']
            identity = dict(pid=71, birth='1234', job_nonce=f'{sequence + 1:032x}',
                            ticket_id=ticket.name,
                            configuration_sha256=hashlib.sha256(archive.canonical(cfg)).hexdigest())
            source = ticket / 'native-owner-current.json'
            save(source, identity)
            binding = spool.commit_job(self.owner, TOKEN, identity['job_nonce'], sequence,
                                       ['worker.exe', '--start', cfg['start']], writer.offset, source)
            self.bindings.append(binding)
            writer.append((('accepted-' + str(sequence)).ljust(63) + '\n').encode())
        seal = writer.finish()
        spool.acknowledge(self.owner / 'output', TOKEN, writer.offset)
        self.final = dict(schema='PGN_OWNER_OUTPUT_V2', token=TOKEN, closed=True, started=True,
                          orphan=False, exit=0, error=None, output_spool=seal,
                          output_job=dict(binding=binding,
                                          binding_sha256=hashlib.sha256(spool._encoded(binding)).hexdigest()))
        save(self.owner / 'done.json', self.final)
        self.reports = {p.name: archive.load(p)['report'] for p in [self.one, self.two]}

    def ticket(self, number):
        folder = self.tickets / f'{number:064x}'
        folder.mkdir()
        cfg = dict(ticket_id=folder.name, start='150000000000000000000', end='150000200000000000000',
                   threshold_policy=dict(sha256='b' * 64))
        computed = dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1', ticket_id=folder.name,
                        start=cfg['start'], end=cfg['end'], positions='200000000000000',
                        policy_sha256='b' * 64, counts=['3'] + ['0'] * 99, best=[])
        report = dict(body=computed, sha256=hashlib.sha256(archive.canonical(computed)).hexdigest(),
                      network_accepted=False)
        frame = dict(ticket_id=folder.name, start=cfg['start'], end=cfg['end'],
                     positions=computed['positions'], phase='Report ready',
                     scopes=dict(ticket=dict(statistics=dict(count_total='3'))))
        body = dict(schema=archive.SCHEMA, configuration=cfg,
                    configuration_sha256=hashlib.sha256(archive.canonical(cfg)).hexdigest(),
                    report=report, frame=frame)
        raw = gzip.compress(archive.canonical(body), mtime=0)
        (folder / archive.ARCHIVE_NAME).write_bytes(raw)
        save(folder / 'report.json', report)
        save(folder / 'session.json', dict(schema=archive.SCHEMA, ticket_id=folder.name,
             archive=archive.ARCHIVE_NAME, sha256=hashlib.sha256(raw).hexdigest()))
        return folder

    def logs(self):
        return sorted((self.owner / 'output').glob('*.log'))

    def test_real_cross_ticket_archive_conservation_and_idempotency(self):
        self.assertEqual(archive.retire_completed_owner_output(self.owner)['reclaimed_bytes'], 128)
        self.assertFalse(self.logs())
        self.assertEqual(archive.retire_completed_owner_output(self.owner)['reclaimed_bytes'], 0)
        for folder in [self.one, self.two]:
            self.assertEqual(archive.load(folder)['report'], self.reports[folder.name])
            self.assertEqual(json.loads((folder / 'report.json').read_bytes()), self.reports[folder.name])
        self.assertTrue((self.owner / 'done.json').exists())
        self.assertEqual(len(list(self.owner.glob('job-*.json'))), 4)

    def test_completed_first_ticket_does_not_authorize_other_ticket_recovery_deletion(self):
        save(self.two / 'session.json', dict(schema='PGN68_DURABLE_SESSION_V1', state='PAUSED'))
        result = archive.retire_completed_owner_output(self.owner)
        self.assertFalse(result['retired'])
        self.assertEqual(len(self.logs()), 2)

    def test_no_ack_partial_ack_and_wrong_ack_retain_output(self):
        ack = self.owner / 'output/ack.json'
        original = json.loads(ack.read_bytes())
        for value in [None, dict(original, end_offset=64), dict(original, sha256='f' * 64),
                      dict(original, end_offset=128.0), dict(original, token='f' * 32)]:
            if value is None:
                ack.unlink()
            else:
                save(ack, value)
            self.assertFalse(archive.retire_completed_owner_output(self.owner)['retired'])
            self.assertEqual(len(self.logs()), 2)

    def test_unsettled_or_orphaned_owner_is_retained(self):
        for changes in [dict(orphan=True), dict(exit=1), dict(exit=False), dict(closed=False),
                        dict(error='undrained'), dict(started=False), dict(schema='historical')]:
            save(self.owner / 'done.json', dict(self.final, **changes))
            self.assertFalse(archive.retire_completed_owner_output(self.owner)['retired'])
            self.assertEqual(len(self.logs()), 2)

    def test_dry_run_does_not_publish_or_remove_anything(self):
        before = {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()}
        result = archive.retire_completed_owner_output(self.owner, dry_run=True)
        self.assertEqual(result['reclaimable_bytes'], 128)
        self.assertEqual(before, {str(p.relative_to(self.root)): p.read_bytes() for p in self.root.rglob('*') if p.is_file()})

    def test_failure_before_receipt_leaves_all_output(self):
        with patch.object(archive, 'atomic_bytes', side_effect=OSError('receipt failure')):
            with self.assertRaises(OSError):
                archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 2)

    def test_interruption_after_receipt_resumes_missing_prefix(self):
        original = Path.unlink
        def interrupted(path, *args, **kwargs):
            if path.name == '000000000001.log':
                raise PermissionError('injected open file')
            return original(path, *args, **kwargs)
        with patch.object(Path, 'unlink', interrupted):
            with self.assertRaises(PermissionError):
                archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 1)
        self.assertTrue((self.owner / 'archived-output.json').exists())
        self.assertEqual(archive.retire_completed_owner_output(self.owner)['reclaimed_bytes'], 64)

    def test_corrupt_output_is_never_removed(self):
        self.logs()[1].write_bytes(b'x' * 64)
        with self.assertRaises(spool.SpoolError):
            archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 2)

    def test_bounded_catalogue_and_malformed_seal_are_refused(self):
        with patch.object(archive, 'MAXIMUM_OWNER_JOBS', 1):
            with self.assertRaisesRegex(ValueError, 'catalogue'):
                archive.retire_completed_owner_output(self.owner)
        for segments in [None, [False], [{}] * 129]:
            seal = dict(self.final['output_spool'], segments=segments)
            save(self.owner / 'output/index.json', seal)
            save(self.owner / 'done.json', dict(self.final, output_spool=seal))
            with self.assertRaises(ValueError):
                archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 2)

    def test_configuration_or_identity_mismatch_is_never_removed(self):
        identity = self.owner / self.bindings[0]['identity']
        identity.write_bytes(identity.read_bytes() + b' ')
        with self.assertRaises(ValueError):
            archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 2)

    def test_bound_ticket_path_injection_is_refused(self):
        binding_path = self.owner / spool._binding_name(0, self.bindings[0]['nonce'])
        identity_path = self.owner / self.bindings[0]['identity']
        identity = json.loads(identity_path.read_bytes())
        identity['ticket_id'] = '../outside'
        save(identity_path, identity)
        binding = json.loads(binding_path.read_bytes())
        binding['identity_sha256'] = hashlib.sha256(identity_path.read_bytes()).hexdigest()
        save(binding_path, binding)
        self.assertFalse(archive.retire_completed_owner_output(self.owner)['retired'])
        self.assertEqual(len(self.logs()), 2)

    def test_batch_invokes_retirement_after_all_archives_exist(self):
        result = archive.compact_batch(self.root)
        self.assertEqual(result['deferred'], 0)
        self.assertEqual(result['owners_retired'], 1)
        self.assertEqual(result['reclaimed_bytes'], 128)
        self.assertFalse(self.logs())

    def test_final_executed_job_must_match_last_binding(self):
        changed = dict(self.final, output_job={})
        save(self.owner / 'done.json', changed)
        with self.assertRaises(ValueError):
            archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 2)

    def test_changed_archive_after_interruption_blocks_retry(self):
        with patch.object(Path, 'unlink', side_effect=PermissionError('open')):
            with self.assertRaises(PermissionError):
                archive.retire_completed_owner_output(self.owner)
        marker = self.two / 'session.json'
        changed = json.loads(marker.read_bytes())
        changed['extra'] = 'authority changed'
        save(marker, changed)
        with self.assertRaises(ValueError):
            archive.retire_completed_owner_output(self.owner)
        self.assertEqual(len(self.logs()), 2)


if __name__ == '__main__':
    unittest.main()
