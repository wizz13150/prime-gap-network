"""Durable bounded CPU preparation, without network or GPU ownership.

One consumer executes a finite attempt. Producers may enqueue while it runs.
An interrupted attempt is retained; only a recorded, dead process permits a
new attempt. Completion is always re-bound to the approved executable/output.
"""
from pathlib import Path
import hashlib
import json
import os
import re

from horizon_preparation_process import (
    PRODUCER_SHA, canonical, regular, run_preparation,
    PreparationRecoveryRequired,
)
from pgn_owned import _kernel, _recorded_alive
from pgn_session import SessionControllerLock, SessionControllerBusy, atomic_bytes

MAX_ITEMS = 16
MAX_ATTEMPTS = 2
MAX_ATTEMPT_RECORDS = 64
MAX_RETAINED_BYTES = 256 * 2**20
MAX_RETAINED_FILES = 2048
MAX_RETAINED_DIRECTORIES = 256


def _directory(path):
    path = Path(path)
    if path.exists() and (path.is_symlink() or getattr(path.lstat(), 'st_file_attributes', 0) & 0x400):
        raise ValueError('Preparation directory cannot be a reparse point')
    path.mkdir(parents=True, exist_ok=True)
    return path


def _scopes(values):
    if type(values) not in (tuple, list) or not 1 <= len(values) <= 64:
        raise ValueError('One through64 bounded preparation scopes required')
    result = []
    prior = total = 0
    for pair in values:
        if type(pair) not in (tuple, list) or len(pair) != 2 or any(type(v) is not int for v in pair):
            raise ValueError('Exact preparation bounds required')
        a, b = pair
        if not 3 <= a < b <= 10**21 or a < prior:
            raise ValueError('Preparation scopes overlap or exceed the supported domain')
        prior = b
        total += b - a
        result.append((a, b))
    if total > 10**15:
        raise ValueError('Preparation batch exceeds its position budget')
    return tuple(result)


class PreparationQueue:
    def __init__(self, root):
        self.root = _directory(root).resolve()
        self.jobs = _directory(self.root / 'jobs')
        self.consumer = _directory(self.root / 'consumer')
        self.index = self.root / 'queue.json'

    def _rows(self):
        rows = json.loads(regular(self.index, 32768)) if self.index.exists() else []
        if type(rows) is not list or len(rows) > MAX_ITEMS:
            raise ValueError('Preparation queue item budget exceeded')
        seen = set()
        for row in rows:
            if (type(row) is not dict or set(row) != {'id', 'attempt', 'state'}
                    or type(row['id']) is not str or not re.fullmatch('[0-9a-f]{64}', row['id'])
                    or row['id'] in seen or type(row['attempt']) is not int
                    or not 0 <= row['attempt'] <= MAX_ATTEMPT_RECORDS
                    or row['state'] not in ('queued', 'running', 'ready', 'failed')
                    or row['state'] != 'queued' and row['attempt'] == 0):
                raise ValueError('Malformed preparation queue reference')
            seen.add(row['id'])
        return rows

    def _save(self, rows):
        raw = canonical(rows) + b'\n'
        if len(raw) > 32768:
            raise BufferError('Preparation queue exceeds its byte budget')
        atomic_bytes(self.index, raw)

    def _job(self, identity):
        if type(identity) is not str or not re.fullmatch('[0-9a-f]{64}', identity):
            raise ValueError('Preparation identity must be a canonical digest')
        _directory(self.root)
        _directory(self.jobs)
        return _directory(self.jobs / identity)

    def _request(self, identity):
        raw = regular(self._job(identity) / 'scopes.json', 32768)
        request = json.loads(raw)
        if (hashlib.sha256(canonical(request)).hexdigest() != identity
                or set(request) != {'schema', 'image_sha256', 'scopes'}
                or request['schema'] != 'PGN_CPU_PREPARATION_SCOPES_V1'
                or request['image_sha256'] != PRODUCER_SHA):
            raise ValueError('Immutable preparation request changed')
        values = request['scopes']
        if type(values) is not list or any(type(p) is not list or len(p) != 2
                or any(type(x) is not str or not re.fullmatch('[1-9][0-9]{0,21}', x) for x in p)
                for p in values):
            raise ValueError('Canonical decimal preparation scopes required')
        return _scopes([[int(x) for x in p] for p in values])

    def _capacity(self):
        total = files = directories = 0
        # Do not follow junctions/symlinks, including entries not in the index.
        def unreadable(error):
            raise error
        for parent, dirs, names in os.walk(self.root, followlinks=False, onerror=unreadable):
            directories += 1
            if directories > MAX_RETAINED_DIRECTORIES:
                raise BufferError('Retained preparation directory budget reached')
            for name in [*dirs, *names]:
                item = Path(parent) / name
                info = item.lstat()
                if item.is_symlink() or getattr(info, 'st_file_attributes', 0) & 0x400:
                    raise ValueError('Retained preparation contains a reparse point')
            for name in names:
                files += 1
                total += (Path(parent) / name).stat().st_size
                if files > MAX_RETAINED_FILES or total > MAX_RETAINED_BYTES:
                    raise BufferError('Retained preparation file/byte budget reached')
        # Reserve the producer's maximum witness stream, log and small metadata.
        if total + 8 * 2**20 > MAX_RETAINED_BYTES:
            raise BufferError('Preparation has no capacity for another bounded attempt')
        if files + 80 > MAX_RETAINED_FILES or directories + 3 > MAX_RETAINED_DIRECTORIES:
            raise BufferError('Preparation has no file/directory capacity for another attempt')

    def enqueue(self, scopes):
        values = _scopes(scopes)
        request = dict(schema='PGN_CPU_PREPARATION_SCOPES_V1', image_sha256=PRODUCER_SHA,
                       scopes=[[str(a), str(b)] for a, b in values])
        encoded = canonical(request)
        identity = hashlib.sha256(encoded).hexdigest()
        lock = SessionControllerLock(self.root)
        try:
            rows = self._rows()
            if any(row['id'] == identity for row in rows):
                self._request(identity)
                return identity
            if len(rows) >= MAX_ITEMS:
                raise BufferError('Preparation queue is full; input must remain with its caller')
            self._capacity()
            path = self._job(identity) / 'scopes.json'
            if path.exists():
                if regular(path, 32768) != encoded + b'\n':
                    raise ValueError('Existing preparation input differs')
            else:
                atomic_bytes(path, encoded + b'\n')
            rows.append(dict(id=identity, attempt=0, state='queued'))
            self._save(rows)
            return identity
        finally:
            lock.release()

    def entries(self):
        lock = SessionControllerLock(self.root)
        try:
            return self._rows()
        finally:
            lock.release()

    def _update(self, identity, **changes):
        lock = SessionControllerLock(self.root)
        try:
            rows = self._rows()
            row = next(row for row in rows if row['id'] == identity)
            row.update(changes)
            self._save(rows)
        finally:
            lock.release()

    @staticmethod
    def _closed_before_identity(attempt):
        """The owned-process constructor closed/drained before publishing a PID.

        This exact local marker is written only after WindowsOwnedProcess.close
        observes child closure and an empty Job. An incomplete/missing marker,
        published identity or any output cannot use this recovery case.
        """
        try:
            owner=json.loads(regular(attempt/'producer-owner.json',32768))
            request=json.loads(regular(attempt/'request.json',32768))
        except (OSError,ValueError):return False
        return (owner==dict(schema='PGN_CPU_PREPARATION_OWNER_V1',role='cpu_preparation',
                           configuration_sha256=hashlib.sha256(canonical(request)).hexdigest(),state='OWNED_PROCESS_CLOSED')
                and request.get('schema')=='PGN_CPU_PREPARATION_REQUEST_V1'and request.get('image_sha256')==PRODUCER_SHA
                and not any((attempt/name).exists()for name in ('producer-identity.json','producer-receipt.json','output')))

    @staticmethod
    def _cancelled_attempt(attempt):
        try:
            marker=json.loads(regular(attempt/'cancelled.json',32768))
            request=regular(attempt/'request.json',32768)
            owner=regular(attempt/'producer-owner.json',32768)
            identity=json.loads(owner)
        except (OSError,ValueError):
            return False
        return (marker==dict(schema='PGN_CPU_PREPARATION_CANCELLED_V1',
                    request_sha256=hashlib.sha256(request).hexdigest(),
                    owner_sha256=hashlib.sha256(owner).hexdigest())
                and identity.get('state')=='OWNED_PROCESS_CLOSED'
                and not (attempt/'producer-receipt.json').exists())

    def _failed_attempts(self,job,number):
        # Number is bounded by the queue schema. Keep all immutable attempt
        # records; cancellation never weakens the failure or storage budgets.
        return sum(not self._cancelled_attempt(job/('attempt-'+str(i)))
                   for i in range(1,number+1))

    @staticmethod
    def _abandon(attempt):
        lock = SessionControllerLock(attempt)
        try:
            # A crash before creating a request did not start computation.
            if not (attempt / 'request.json').exists():
                return
            owner_file = attempt / 'producer-owner.json'
            if not owner_file.exists():
                raise PreparationRecoveryRequired('Missing process identity; retain attempt for explicit reconciliation')
            owner = json.loads(regular(owner_file, 32768))
            request = json.loads(regular(attempt / 'request.json', 32768))
            if PreparationQueue._closed_before_identity(attempt):
                atomic_bytes(attempt/'abandoned.json',canonical(dict(
                    schema='PGN_CPU_PREPARATION_ABANDONED_V1',closed_before_identity=True,
                    owned_job_drained=True,partial_outputs_retained=True))+b'\n')
                return
            if (owner.get('schema') != 'PGN_CPU_PREPARATION_OWNER_V1'
                    or owner.get('role') != 'cpu_preparation'
                    or type(owner.get('pid')) is not int or not 0 < owner['pid'] <= 0xffffffff
                    or type(owner.get('birth')) is not str or not owner['birth'].isdigit()
                    or owner.get('image_sha256') != PRODUCER_SHA
                    or request.get('image_sha256') != PRODUCER_SHA
                    or owner.get('configuration_sha256') != hashlib.sha256(canonical(request)).hexdigest()
                    or owner.get('command') != request.get('args')):
                raise PreparationRecoveryRequired('Invalid owned process identity; no automatic retry')
            if os.name != 'nt':
                raise PreparationRecoveryRequired('Interrupted preparation recovery currently requires Windows')
            kernel, types = _kernel()
            if _recorded_alive(kernel, types, owner):
                raise PreparationRecoveryRequired('Prior preparation process is still alive')
            atomic_bytes(attempt / 'abandoned.json', canonical(dict(
                schema='PGN_CPU_PREPARATION_ABANDONED_V1', pid=owner['pid'], birth=owner['birth'],
                process_identity_dead=True, partial_outputs_retained=True)) + b'\n')
        finally:
            lock.release()

    def pump(self, worker, *, logical_cpu, memory_mib=768, timeout=1800, cancelled=None):
        """Execute/adopt one input; return its id, or None when idle/busy.

        Call from the resource coordinator's CPU lane, never the UI thread.
        The queue index lock is released throughout the expensive execution.
        """
        _directory(self.consumer)
        try:
            consumer = SessionControllerLock(self.consumer)
        except SessionControllerBusy:
            return None
        try:
            row = next((row for row in self.entries() if row['state'] in ('queued', 'running')
                        or (row['state']=='failed'and 0<row['attempt']<MAX_ATTEMPT_RECORDS
                             and self._failed_attempts(self._job(row['id']),row['attempt'])<MAX_ATTEMPTS
                            and self._closed_before_identity(self._job(row['id'])/('attempt-'+str(row['attempt']))))), None)
            if row is None:
                return None
            identity = row['id']
            scopes = self._request(identity)
            if cancelled is not None and cancelled():
                raise InterruptedError('Preparation remains queued')
            job = self._job(identity)
            number = row['attempt']
            attempt = _directory(job / ('attempt-' + str(number))) if number else None
            if attempt is None or not (attempt / 'producer-receipt.json').exists():
                if attempt is not None:
                    try:
                        self._abandon(attempt)
                    except PreparationRecoveryRequired:
                        self._update(identity, state='failed')
                        raise
                if (number >= MAX_ATTEMPT_RECORDS
                        or self._failed_attempts(job,number) >= MAX_ATTEMPTS):
                    self._update(identity, state='failed')
                    raise PreparationRecoveryRequired('Preparation attempt budget reached; partial work retained')
                self._capacity()
                number += 1
                attempt = _directory(job / ('attempt-' + str(number)))
            self._update(identity, attempt=number, state='running')
            try:
                run_preparation(worker, attempt, scopes, logical_cpu=logical_cpu,
                                memory_mib=memory_mib, timeout=timeout, cancelled=cancelled)
            except (InterruptedError, TimeoutError):
                self._update(identity, state='queued')
                raise
            self._update(identity, state='ready')
            return identity
        finally:
            consumer.release()

    def ready(self, identity, worker, *, logical_cpu):
        row = next((row for row in self.entries() if row['id'] == identity), None)
        if row is None or row['state'] != 'ready':
            raise PreparationRecoveryRequired('Requested source is not ready')
        attempt = self._job(identity) / ('attempt-' + str(row['attempt']))
        if not (attempt / 'producer-receipt.json').is_file() or not (attempt / 'ready.json').is_file():
            raise PreparationRecoveryRequired('Ready source lost its completion receipt; no hidden recomputation')
        return run_preparation(worker, attempt,
                               self._request(identity), logical_cpu=logical_cpu)

    def acknowledge(self, identity):
        """Release the queue reference after handoff; never delete source evidence."""
        lock = SessionControllerLock(self.root)
        try:
            rows = self._rows()
            row = next((row for row in rows if row['id'] == identity), None)
            if row is None:
                return
            if row['state'] != 'ready':
                raise PreparationRecoveryRequired('Only completed source may leave the queue')
            self._save([entry for entry in rows if entry['id'] != identity])
        finally:
            lock.release()
