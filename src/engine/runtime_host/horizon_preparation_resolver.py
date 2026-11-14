"""Batch issued audit/search scopes into the off-GPU source preparation lane.

Registration only journals inputs and wakes an optional resident preparation
lane. Heavy preparation never runs in GPU callbacks or UI.
"""
from pathlib import Path
from contextlib import contextmanager
import json
import re
import threading
import time

from horizon_preparation_process import canonical, regular, PreparationRecoveryRequired
from horizon_preparation_queue import _directory
from horizon_preparation_scopes import LOW, DOMAIN
from pgn_session import SessionControllerLock, SessionControllerBusy, atomic_bytes, folder_lock


class PreparationResolver:
    def __init__(self, root, provider, *, logical_cpu, memory_mib=768, timeout=1800, stopping=None, prefetch=False):
        from pgn_owned import _job_limit_bytes
        _job_limit_bytes(dict(logical_cpu=logical_cpu, memory_mib=memory_mib))
        if type(timeout) not in (int, float) or not 0 < timeout <= 3600:
            raise ValueError('Finite source preparation timeout required')
        self.root = _directory(root).resolve()
        self.consumer = _directory(self.root / 'consumer')
        self.file = self.root / 'work.json'
        self.intent = self.root / 'batch-intent.json'
        self.ack_intent = self.root / 'ack-intent.json'
        self.provider = provider
        self.resources = dict(logical_cpu=logical_cpu, memory_mib=memory_mib, timeout=timeout)
        self.stopping = stopping if stopping is not None else threading.Event()
        if type(prefetch)is not bool:raise ValueError('Explicit preparation prefetch choice required')
        self._closed=threading.Event();self._wake=threading.Event();self._thread=None
        self.last_prefetch_error=None
        if prefetch:
            self._thread=threading.Thread(target=self._prefetch,name='scientific-source-preparation',daemon=True)
            if self.file.exists():self._wake.set()
            self._thread.start()

    def _stopped(self):
        return self._closed.is_set()or self.stopping.is_set()

    def bind_run_control(self, enabled):
        self.enabled = enabled
        self._wake.set()

    def _cancelled(self):
        enabled = getattr(self, 'enabled', None)
        return self._stopped() or enabled is not None and not enabled.is_set()

    def _pump_once(self):
        consumer=SessionControllerLock(self.consumer)
        try:
            if self._cancelled():raise InterruptedError('Source preparation retained for restart')
            self._batch()
            if self._cancelled():raise InterruptedError('Source preparation retained for restart')
            return self.provider.pump(**self.resources,cancelled=self._cancelled)
        finally:consumer.release()

    def _prefetch(self):
        while not self._stopped():
            enabled = getattr(self, 'enabled', None)
            if enabled is not None and not enabled.wait(.2):
                continue
            if not self._wake.wait(.2):continue
            self._wake.clear()
            while not self._cancelled():
                try:
                    if not self._pump_once():break
                    self.last_prefetch_error=None
                except SessionControllerBusy:
                    self._closed.wait(.2)
                except Exception as error:
                    self.last_prefetch_error=str(error)[:400]
                    # Preserve the durable failed/recoverable job. A later
                    # registration or explicit resolve may retry eligible work.
                    break
            # OFF cancels one preparation, not the resident preparation lane.
            # Preserve its wake until a later ON resumes the retained inputs.
            if not self._stopped() and self._cancelled():
                self._wake.set()

    def close(self):
        self._closed.set();self._wake.set()
        if self._thread is not None:self._thread.join(5)
        return self._thread is None or not self._thread.is_alive()

    @contextmanager
    def _journal(self):
        # Audit/report finalizers share one process. Serialize their short
        # journal mutations before taking the nonblocking cross-process lock.
        with folder_lock(self.root):
            lock = SessionControllerLock(self.root)
            try:
                yield
            finally:
                lock.release()

    def _rows(self):
        rows = json.loads(regular(self.file, 32768)) if self.file.exists() else []
        if type(rows) is not list or len(rows) > 64:
            raise ValueError('Source resolver work budget exceeded')
        seen = set()
        for row in rows:
            if (type(row) is not dict or set(row) != {'work_id', 'start', 'end', 'plan'}
                    or type(row['work_id']) is not str or not re.fullmatch('[0-9a-f]{64}', row['work_id'])
                    or row['work_id'] in seen or type(row['start']) is not str or type(row['end']) is not str
                    or not re.fullmatch('[1-9][0-9]{0,21}', row['start'])
                    or not re.fullmatch('[1-9][0-9]{0,21}', row['end'])
                    or not LOW <= int(row['start']) < int(row['end']) <= DOMAIN
                    or row['plan'] is not None and (type(row['plan']) is not str or not re.fullmatch('[0-9a-f]{64}', row['plan']))):
                raise ValueError('Malformed immutable source resolver work')
            seen.add(row['work_id'])
        return rows

    def register(self, tickets):
        from horizon_engine_host import ticket_configuration
        if self._stopped():raise InterruptedError('Source registration closed; issued work retained by caller')
        if type(tickets) not in (list, tuple) or not 1 <= len(tickets) <= 64:
            raise ValueError('One through64 issued source jobs required')
        inputs = []
        for ticket in tickets:
            cfg = ticket_configuration(ticket)
            if not LOW <= int(cfg['start']) < int(cfg['end']) <= DOMAIN:
                raise ValueError('Issued work escapes the supported residual domain')
            inputs.append(dict(work_id=cfg['ticket_id'], start=cfg['start'], end=cfg['end'], plan=None))
        with self._journal():
            self._finish_ack()
            rows = self._rows()
            for item in inputs:
                old = next((row for row in rows if row['work_id'] == item['work_id']), None)
                if old is not None:
                    if (old['start'], old['end']) != (item['start'], item['end']):
                        raise ValueError('Issued source work changed its immutable bounds')
                else:
                    if len(rows) >= 64:
                        raise BufferError('Source resolver full; caller retains issued work')
                    rows.append(item)
            atomic_bytes(self.file, canonical(rows) + b'\n')
        self._wake.set()

    def _batch(self):
        with self._journal():
            self._finish_ack()
            rows = self._rows()
            if self.intent.exists():
                requests = json.loads(regular(self.intent, 32768))
                if type(requests) is not list or not 1 <= len(requests) <= 64:
                    raise ValueError('Invalid durable source batch intent')
                for item in requests:
                    if type(item) is not dict or set(item) != {'work_id', 'start', 'end'}:
                        raise ValueError('Malformed source batch intent')
                    old = next((row for row in rows if row['work_id'] == item['work_id']), None)
                    if old is None or item != dict(work_id=old['work_id'], start=int(old['start']), end=int(old['end'])):
                        raise ValueError('Source batch intent differs from issued work')
            else:
                requests = [dict(work_id=row['work_id'], start=int(row['start']), end=int(row['end']))
                            for row in rows if row['plan'] is None]
                if not requests:
                    return
                atomic_bytes(self.intent, canonical(requests) + b'\n')
            identity, plan = self.provider.request(requests)
            selected = {work.work_id for work in plan.selected}
            for row in rows:
                if row['work_id'] in selected:
                    if row['plan'] not in (None, identity):
                        raise ValueError('Prepared source assignment changed during recovery')
                    row['plan'] = identity
            atomic_bytes(self.file, canonical(rows) + b'\n')
            # A crash before this unlink replays the exact same batch. provider
            # request/enqueue are idempotent, so no second scan is introduced.
            self.intent.unlink()

    def _work(self, configuration):
        with self._journal():
            self._finish_ack()
            row = next((row for row in self._rows() if row['work_id'] == configuration.get('ticket_id')), None)
            if row is None or (row['start'], row['end']) != (configuration.get('start'), configuration.get('end')):
                raise ValueError('Finalization source was not registered as the same issued work')
            return row

    def _finish_ack(self):
        """Caller holds the short journal lock; replay only this exact release."""
        if not self.ack_intent.exists():
            return
        intent = json.loads(regular(self.ack_intent, 32768))
        if type(intent) is not dict or set(intent) != {'schema', 'work', 'job'} or intent['schema'] != 'PGN_PREPARATION_HANDOFF_V1':
            raise ValueError('Malformed source handoff intent')
        work = intent['work']
        if (type(work) is not dict or set(work) != {'work_id', 'start', 'end', 'plan'}
                or type(work.get('plan')) is not str or not re.fullmatch('[0-9a-f]{64}', work['plan'])):
            raise ValueError('Source handoff lost its prepared work identity')
        plan, job, _ = self.provider._input(work['plan'])
        planned = next((value for value in plan.selected if value.work_id == work['work_id']), None)
        if (job != intent['job'] or planned is None
                or (work['start'], work['end']) != (str(planned.start), str(planned.end))):
            raise ValueError('Source handoff changed its producer job')
        entry = next((row for row in self.provider.queue.entries() if row['id'] == job), None)
        if entry is not None and entry['state'] != 'ready':
            raise PreparationRecoveryRequired('Source handoff has no completed producer')
        rows = self._rows()
        old = next((row for row in rows if row['work_id'] == work['work_id']), None)
        if old is not None and old != work:
            raise ValueError('Source work changed during handoff recovery')
        remaining = [row for row in rows if row['work_id'] != work['work_id']]
        # Different plans can deliberately share the same mathematical scope.
        # Release its queue reference only after every assigned consumer left.
        jobs = {self.provider._input(plan)[1] for plan in {row['plan'] for row in remaining if row['plan'] is not None}}
        atomic_bytes(self.file, canonical(remaining) + b'\n')
        if job not in jobs:
            self.provider.queue.acknowledge(job)
        if not any(row['plan'] == work['plan'] for row in remaining):
            retire = getattr(self.provider, 'retire', None)
            if callable(retire):
                retire(work['plan'])
        self.ack_intent.unlink()

    def acknowledge(self, work_id):
        """Only call after a report seal or corrected audit count is durable."""
        if type(work_id) is not str or not re.fullmatch('[0-9a-f]{64}', work_id):
            raise ValueError('Exact completed work identity required')
        with self._journal():
            self._finish_ack()
            work = next((row for row in self._rows() if row['work_id'] == work_id), None)
            if work is None:
                return
            if work['plan'] is None:
                raise PreparationRecoveryRequired('Unprepared source cannot be released')
            _, job, _ = self.provider._input(work['plan'])
            entry = next((row for row in self.provider.queue.entries() if row['id'] == job), None)
            if entry is None or entry['state'] != 'ready':
                raise PreparationRecoveryRequired('Only completed source may leave the active resolver')
            atomic_bytes(self.ack_intent, canonical(dict(schema='PGN_PREPARATION_HANDOFF_V1', work=work, job=job)) + b'\n')
            self._finish_ack()

    def __call__(self, configuration):
        deadline = time.monotonic() + 2 * self.resources['timeout'] + 10
        while not self._cancelled():
            row = self._work(configuration)
            if row['plan'] is not None:
                _, job, _ = self.provider._input(row['plan'])
                entry = next((value for value in self.provider.queue.entries() if value['id'] == job), None)
                if entry is None or entry['state'] == 'failed':
                    raise PreparationRecoveryRequired('Assigned source needs explicit recovery; original work retained')
                if entry['state'] == 'ready':
                    return self.provider.prepared(row['plan'], logical_cpu=self.resources['logical_cpu'])
            if time.monotonic() >= deadline:
                raise TimeoutError('Source resolver wait budget reached; input retained')
            try:
                self._pump_once()
            except SessionControllerBusy:
                self.stopping.wait(.2)
        raise InterruptedError('Source finalization retained for application restart')
