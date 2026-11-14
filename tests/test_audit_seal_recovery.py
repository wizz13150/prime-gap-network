"""Real audit catalogue/files recover one immutable seal; no GPU or network."""
from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import hashlib
import json
import errno
import os
import sys
import tempfile
import threading
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol')]
from horizon_network_audits import NetworkAudits, save
from horizon_audit_answers import AuditAnswers, AuditSealRecoveryPending, AuditSealRecoveryRequired
from pgn_paths import state_path
from report_commitment import audit_commitment, canonical_bytes


class SealRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.request = '1' * 64
        self.ticket = '2' * 64
        self.ranges = [dict(index=i, start=str(150000000000000000000+i*2000000000000), end=str(150000000000000000000+(i+1)*2000000000000)) for i in range(5)]
        self.answer = audit_commitment(self.ticket, 0, list(range(5)), ['7']*5, '3' * 64)
        self.audit = dict(schema='PGN_HORIZON_ASSIGNED_AUDIT_V1', request_id=self.request,
                          ticket=self.ticket, report_root='4' * 64, slot=1, attempt=0,
                          ranges=deepcopy(self.ranges), counts=[7]*5, target_merit_q16=28 * 65536,
                          answer=deepcopy(self.answer))
        running = threading.Event()
        running.set()
        self.compute_calls = 0
        self.commits = []
        def compute(*args):
            self.compute_calls += 1
            raise AssertionError('A sealed audit cannot run GPU work again')
        worker = SimpleNamespace(root=self.root, source=SimpleNamespace(run_ticket=compute),
                                 running=running, network_lock=threading.RLock(),
                                 args=SimpleNamespace(piece_limit=10000000000000), rpc=None)
        self.audits = NetworkAudits(worker)
        self.audits.state = lambda request: dict(request_id=request, jobs=[dict(slot=1, attempt=0, state='assigned')])
        def apply(action, request=None, slot=None, attempt=None, body=None, **kwargs):
            self.assertEqual(action, 'commit')
            self.commits.append((request, slot, attempt, body))
            return dict(available=True)
        self.audits.apply = apply
        self.name = self.request + '-1-0'
        self.named = self.audits.folder / (self.name + '.json')
        self.current = self.audits.folder / 'current.json'
        save(self.named, self.audit)
        save(self.current, self.audit)
        self.audits.answers.register([self.name])

    def digest(self):
        with self.audits.answers.database() as db:
            return db.execute('SELECT digest FROM answers WHERE name=?', (self.name,)).fetchone()[0]

    def run_without_nonce_or_gpu(self):
        with patch('horizon_network_audits.secrets.token_hex', side_effect=AssertionError('A registered seal must retain its nonce')):
            self.assertEqual(self.audits.process_one(), 'committed')
        saved = json.loads(self.named.read_bytes())
        self.assertEqual(saved['answer'], self.answer)
        self.assertEqual(saved['counts'], [7]*5)
        self.assertEqual(self.compute_calls, 0)
        self.assertEqual(self.commits, [(self.request, 1, 0, self.answer['commitment'])])

    def test_current_lost_answer_reuses_registered_seal_without_publication_intent(self):
        partial = deepcopy(self.audit)
        partial.pop('answer')
        save(self.current, partial)
        before = self.digest()
        self.run_without_nonce_or_gpu()
        self.assertEqual(self.digest(), before)

    def test_bad_named_and_current_answer_recover_original_retained_assignment(self):
        original = self.named.read_bytes()
        # Real pre-fix installations have the digest but no independent copy.
        (self.audits.folder / 'sealed-answers' / (self.name + '.json')).unlink()
        retained = self.audits.folder / 'retained-assignments'
        retained.mkdir()
        (retained / (hashlib.sha256(original).hexdigest() + '.json')).write_bytes(original)
        bad = deepcopy(self.audit)
        bad['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7']*5, '5' * 64)
        save(self.named, bad)
        save(self.current, bad)
        before = self.digest()
        self.run_without_nonce_or_gpu()
        self.assertEqual(self.digest(), before)

    def remove_registration(self):
        with self.audits.answers.database() as db:
            db.execute('DELETE FROM answers WHERE name=?', (self.name,))
        (self.audits.folder / 'sealed-answers' / (self.name + '.json')).unlink(missing_ok=True)

    def test_crash_after_copy_before_catalogue_does_not_allow_second_nonce(self):
        self.remove_registration()
        real = self.audits.answers.retain_seal
        def crash(*args):
            real(*args)
            raise OSError('Injected after complete seal publication')
        with patch.object(self.audits.answers, 'retain_seal', side_effect=crash):
            with self.assertRaisesRegex(OSError, 'Injected'):
                self.audits.answers.register([self.name])
        with self.audits.answers.database() as db:
            self.assertIsNone(db.execute('SELECT digest FROM answers WHERE name=?', (self.name,)).fetchone())
        bad = deepcopy(self.audit)
        bad['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        save(self.named, bad)
        with self.assertRaisesRegex(AuditSealRecoveryRequired, 'changed after registration'):
            self.audits.answers.register([self.name])
        save(self.named, self.audit)
        partial = deepcopy(self.audit)
        partial.pop('answer')
        save(self.current, partial)
        self.run_without_nonce_or_gpu()

    def test_failed_copy_flush_never_registers_partial_seal(self):
        self.remove_registration()
        with patch('horizon_audit_answers.os.fsync', side_effect=OSError('Injected full storage')):
            with self.assertRaisesRegex(OSError, 'full storage'):
                self.audits.answers.register([self.name])
        with self.audits.answers.database() as db:
            self.assertIsNone(db.execute('SELECT digest FROM answers WHERE name=?', (self.name,)).fetchone())
        self.assertFalse((self.audits.folder / 'sealed-answers' / (self.name + '.json')).exists())
        self.assertEqual(list((self.audits.folder / 'sealed-answers').iterdir()), [])

    def test_concurrent_different_nonces_cannot_clobber_first_seal(self):
        self.remove_registration()
        other = deepcopy(self.audit)
        other['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        barrier = threading.Barrier(2)
        outcomes = []
        def writer(value):
            barrier.wait()
            digest = hashlib.sha256(canonical_bytes(value['answer'])).hexdigest()
            try:
                self.audits.answers.retain_seal(self.name, value, digest)
                outcomes.append(('published', value['answer']))
            except AuditSealRecoveryRequired:
                outcomes.append(('refused', value['answer']))
        threads = [threading.Thread(target=writer, args=(value,)) for value in (self.audit, other)]
        for thread in threads:thread.start()
        for thread in threads:thread.join(3)
        self.assertFalse(any(thread.is_alive() for thread in threads))
        self.assertEqual(sorted(kind for kind, _ in outcomes), ['published', 'refused'])
        saved = json.loads((self.audits.folder / 'sealed-answers' / (self.name + '.json')).read_bytes())
        self.assertEqual(saved['answer'], next(answer for kind, answer in outcomes if kind == 'published'))

    @unittest.skipUnless(os.name == 'nt', 'Windows sharing/access error compatibility')
    def test_transient_read_denied_keeps_original_seal(self):
        target=self.audits.folder/'sealed-answers'/(self.name+'.json')
        before=target.read_bytes();real=type(target).open;calls=[]
        def briefly_denied(path,*args,**kwargs):
            calls.append(str(path))
            if len(calls)==1:raise PermissionError(errno.EACCES,'sharing handle')
            return real(path,*args,**kwargs)
        with patch.object(type(target),'open',briefly_denied):
            recovered,digest=self.audits.answers.read_path(self.name,target)
        self.assertEqual(len(calls),2);self.assertEqual(recovered,self.audit)
        self.assertEqual(target.read_bytes(),before);self.assertEqual(digest,self.digest())

    @unittest.skipUnless(os.name == 'nt', 'Real Windows exclusive sharing handle')
    def test_real_windows_sharing_handle_can_expire_without_losing_seal(self):
        import ctypes,time
        from ctypes import wintypes
        target=self.audits.folder/'sealed-answers'/(self.name+'.json')
        before=target.read_bytes();kernel=ctypes.WinDLL('kernel32',use_last_error=True)
        kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
        kernel.CreateFileW.restype=wintypes.HANDLE
        kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
        handle=kernel.CreateFileW(str(target),0x80000000,0,None,3,0,None)
        self.assertNotEqual(handle,ctypes.c_void_p(-1).value)
        release=threading.Event()
        def unlock():
            release.wait(.02);kernel.CloseHandle(handle)
        thread=threading.Thread(target=unlock)
        try:
            with self.assertRaises(PermissionError):target.read_bytes()
            thread.start();recovered,digest=self.audits.answers.read_path(self.name,target)
            self.assertEqual(recovered,self.audit)
        finally:
            if thread.ident is None:kernel.CloseHandle(handle)
            else:release.set();thread.join(1)
        self.assertEqual(target.read_bytes(),before);self.assertEqual(digest,self.digest())

    def test_persistent_denied_or_missing_read_is_bounded_and_keeps_seal(self):
        target=self.audits.folder/'sealed-answers'/(self.name+'.json')
        before=target.read_bytes()
        with patch.object(type(target),'open',side_effect=PermissionError(errno.EACCES,'denied'))as opened:
            with patch('horizon_audit_answers.time.sleep')as sleep:
                with self.assertRaises(PermissionError):self.audits.answers.read_path(self.name,target)
        self.assertEqual(opened.call_count,5 if os.name=='nt'else 1)
        self.assertEqual(sleep.call_count,4 if os.name=='nt'else 0)
        with patch.object(type(target),'open',side_effect=FileNotFoundError(errno.ENOENT,'missing'))as opened:
            with self.assertRaises(FileNotFoundError):self.audits.answers.read_path(self.name,target)
        self.assertEqual(opened.call_count,1);self.assertEqual(target.read_bytes(),before)

    @unittest.skipUnless(os.name == 'nt', 'Native Windows no-replace rename contract')
    def test_windows_registration_and_recovery_need_no_hardlink(self):
        self.remove_registration()
        with patch('horizon_audit_answers.os.link', side_effect=OSError(errno.EOPNOTSUPP, 'No hardlink support')):
            self.audits.answers.register([self.name])
            partial = deepcopy(self.audit)
            partial.pop('answer')
            save(self.current, partial)
            self.run_without_nonce_or_gpu()

    @unittest.skipUnless(os.name == 'nt', 'Native Windows destination collision')
    def test_windows_destination_created_after_check_is_not_replaced(self):
        self.remove_registration()
        original = canonical_bytes(self.audit) + b'\n'
        other = deepcopy(self.audit)
        other['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        real_rename = os.rename
        collisions = []
        def competitor(source, destination):
            # A complete competing seal appears after retain_seal's exists
            # check but before the real Win32 rename of the other nonce.
            Path(destination).write_bytes(original)
            collisions.append(str(destination))
            return real_rename(source, destination)
        with patch('horizon_audit_answers.os.rename', side_effect=competitor):
            with self.assertRaisesRegex(AuditSealRecoveryRequired, 'changed after registration'):
                self.audits.answers.retain_seal(self.name, other, hashlib.sha256(canonical_bytes(other['answer'])).hexdigest())
        self.assertEqual(len(collisions), 1)
        self.assertEqual((self.audits.answers.folder / 'sealed-answers' / (self.name + '.json')).read_bytes(), original)

    def test_missing_original_is_actionable_and_does_not_acknowledge(self):
        (self.audits.folder / 'sealed-answers' / (self.name + '.json')).unlink()
        bad = deepcopy(self.audit)
        bad['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        save(self.named, bad)
        save(self.current, bad)
        before = self.named.read_bytes()
        with patch('horizon_network_audits.secrets.token_hex', side_effect=AssertionError('No replacement nonce')):
            with self.assertRaisesRegex(AuditSealRecoveryRequired, 'Original registered audit seal unavailable') as caught:
                self.audits.process_one()
        self.assertFalse(caught.exception.retryable)
        self.assertEqual(self.named.read_bytes(), before)
        with self.audits.answers.database() as db:
            self.assertEqual(db.execute('SELECT waiting FROM answers WHERE name=?', (self.name,)).fetchone()[0], 1)
        self.assertEqual(self.commits, [])

    def test_legacy_scan_is_bounded_and_preserves_files_while_pending(self):
        (self.audits.folder / 'sealed-answers' / (self.name + '.json')).unlink()
        original = self.named.read_bytes()
        retained = self.audits.folder / 'retained-assignments'
        retained.mkdir()
        files = []
        for serial in range(17):
            raw = canonical_bytes(dict(unrelated=serial))
            file = retained / (hashlib.sha256(raw).hexdigest() + '.json')
            file.write_bytes(raw)
            files.append(file)
        file = retained / (hashlib.sha256(original).hexdigest() + '.json')
        file.write_bytes(original)
        files.append(file)
        bad = deepcopy(self.audit)
        bad['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        save(self.named, bad)
        save(self.current, bad)
        before = self.named.read_bytes()
        ordinary = Path.iterdir
        with patch.object(Path, 'iterdir', lambda path: iter(files) if path.name == 'retained-assignments' else ordinary(path)):
            with self.assertRaises(AuditSealRecoveryPending):
                self.audits.process_one()
            self.assertEqual(self.named.read_bytes(), before)
            self.assertEqual(self.commits, [])
            self.run_without_nonce_or_gpu()

    def test_fresh_answer_still_seals_once_without_gpu_recomputation(self):
        self.remove_registration()
        self.named.unlink()
        partial = deepcopy(self.audit)
        partial.pop('answer')
        save(self.current, partial)
        with patch('horizon_network_audits.secrets.token_hex', return_value='3' * 64) as nonce:
            self.assertEqual(self.audits.process_one(), 'committed')
            nonce.assert_called_once_with(32)
        self.assertEqual(json.loads(self.named.read_bytes())['answer'], self.answer)
        self.assertEqual(self.compute_calls, 0)

    def test_restart_background_poll_repairs_bad_legacy_name_before_lane(self):
        original = self.named.read_bytes()
        (self.audits.folder / 'sealed-answers' / (self.name + '.json')).unlink()
        retained = self.audits.folder / 'retained-assignments'
        retained.mkdir()
        (retained / (hashlib.sha256(original).hexdigest() + '.json')).write_bytes(original)
        bad = deepcopy(self.audit)
        bad['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        save(self.named, bad)
        save(self.current, bad)
        before = self.digest()
        restarted = AuditAnswers(self.audits.folder)
        restarted.migrate_step()
        self.assertEqual(restarted.next_page(1), [(self.name, self.audit)])
        self.assertEqual(self.digest(), before)
        self.audits.answers = restarted
        self.run_without_nonce_or_gpu()

    def test_long_native_windows_path_keeps_registration_and_recovery(self):
        folder = state_path(self.root / ('a' * 110) / ('b' * 110) / 'audit-lane')
        folder.mkdir(parents=True)
        catalog = AuditAnswers(folder)
        path = catalog.folder / (self.name + '.json')
        self.assertGreater(len(str(path)), 260)
        save(path, self.audit)
        catalog.register([self.name])
        bad = deepcopy(self.audit)
        bad['answer'] = audit_commitment(self.ticket, 0, list(range(5)), ['7'] * 5, '5' * 64)
        save(path, bad)
        restored = AuditAnswers(folder).recover_seal(self.name, bad)
        self.assertEqual(restored['answer'], self.answer)
        with catalog.database() as db:
            self.assertEqual(db.execute('SELECT waiting FROM answers WHERE name=?', (self.name,)).fetchone()[0], 1)

    def test_sealed_binding_conflict_is_refused_before_overwriting_original(self):
        before = self.named.read_bytes()
        partial = deepcopy(self.audit)
        partial.pop('answer')
        partial['report_root'] = '6' * 64
        save(self.current, partial)
        with patch('horizon_network_audits.secrets.token_hex', side_effect=AssertionError('Must fail before generating nonce')):
            with self.assertRaisesRegex(ValueError, 'differs'):
                self.audits.process_one()
        self.assertEqual(self.named.read_bytes(), before)
        self.assertEqual(self.compute_calls, 0)
        self.assertEqual(self.commits, [])


if __name__ == '__main__':
    unittest.main()
