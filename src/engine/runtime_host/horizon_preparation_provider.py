"""Bind planned work, owned residual preparation and exact report refinement.

The provider does not allocate network work, choose another engine or mint.
Its oracle must prove primality in the selected domain. The native primary
still owes its qualified small-factor exclusion contract.
"""
from dataclasses import dataclass
from contextlib import contextmanager
from pathlib import Path
import hashlib
import json
import re
from itertools import islice

from horizon_preparation_process import canonical, regular, PreparationRecoveryRequired, run_preparation, PRODUCER_SHA
from horizon_preparation_queue import PreparationQueue, _directory, MAX_ATTEMPT_RECORDS
from horizon_preparation_retention import PreparationRetention
from horizon_preparation_scopes import (
    PreparationPlan, PlannedWork, plan_preparation, _union, LOW, DOMAIN,
)
from horizon_complement_feed import prepare, ComplementFeedBank
from horizon_complement_runtime import ComplementRuntime,qualified_complement_pair,TABLE_SHA
from report_gap_repair import prepare as refine_report
from psp_batch import euler_jacobi
from pgn_session import SessionControllerLock, atomic_bytes, folder_lock


def _digest(value):
    if type(value) is not str or not re.fullmatch('[0-9a-f]{64}', value):
        raise ValueError('Canonical work/plan identity required')
    return value


def _integer(value):
    if type(value) is not str or not re.fullmatch('[1-9][0-9]{0,21}', value):
        raise ValueError('Canonical bounded decimal required')
    return int(value)


def _decode(document):
    if (type(document) is not dict or document.get('schema') != 'PGN_COMPLEMENT_PREPARATION_PLAN_V1'
            or document.get('producer_executed') is not False or document.get('network_admitted') is not False
            or type(document.get('selected')) is not list or not 1 <= len(document['selected']) <= 64
            or type(document.get('deferred')) is not list or len(document['deferred']) > 64
            or type(document.get('scopes')) is not list or not 1 <= len(document['scopes']) <= 64):
        raise ValueError('Bounded immutable preparation plan required')
    selected = []
    for row in document['selected']:
        if type(row) is not dict or set(row) != {'work_id', 'start', 'end', 'left_prime', 'right_prime', 'scope'}:
            raise ValueError('Malformed planned work')
        if type(row['scope']) is not int or not 0 <= row['scope'] < len(document['scopes']):
            raise ValueError('Exact planned scope index required')
        work = PlannedWork(_digest(row['work_id']), *[_integer(row[k]) for k in
                           ('start', 'end', 'left_prime', 'right_prime')])
        if not LOW <= work.start < work.end <= DOMAIN or not 2 <= work.left_prime <= work.start < work.end <= work.right_prime < DOMAIN:
            raise ValueError('Planned work escapes its supported prime-ended domain')
        selected.append(work)
    deferred = tuple(_digest(value) for value in document['deferred'])
    identities = [work.work_id for work in selected] + list(deferred)
    if len(identities) != len(set(identities)) or len(identities) > 64:
        raise ValueError('Planned identities overlap or exceed capacity')
    scopes = []
    for row in document['scopes']:
        if type(row) is not dict or set(row) != {'start', 'end'}:
            raise ValueError('Malformed planned scope')
        scopes.append((_integer(row['start']), _integer(row['end'])))
    plan = PreparationPlan(tuple(selected), deferred, tuple(scopes))
    if plan.scopes != _union(selected) or not 0 < plan.covered_positions <= 10**15 or plan.document() != document:
        raise ValueError('Plan projection, scope ownership or position budget changed')
    return plan


@dataclass(frozen=True)
class PreparedSourcePlan:
    plan: PreparationPlan
    bundle: object
    feeds: ComplementFeedBank
    exact_prime: object

    def refine(self, report, configuration, primary, *, cancelled=None):
        def check():
            if cancelled is not None and cancelled():
                raise InterruptedError('Source refinement retained while mining is OFF')
        def prime(value):
            check()
            result = self.exact_prime(value)
            check()
            return result
        check()
        work = next((work for work in self.plan.selected
                     if work.work_id == configuration.get('ticket_id')), None)
        if (work is None or configuration.get('start') != str(work.start)
                or configuration.get('end') != str(work.end)
                or configuration.get('preseal_required') is not True):
            raise ValueError('Retained report does not belong to prepared work with source-index retention')
        index = next(i for i, (a, b) in enumerate(self.plan.scopes)
                     if a <= work.left_prime <= work.start < work.end <= work.right_prime < b)
        feed = self.feeds.select(work.start, work.end)
        if configuration.get('complement_source_mode')=='DEFERRED_RESIDUAL_PRESEAL_V1':
            if (not qualified_complement_pair(configuration.get('complement_primary_image_sha256'),
                    configuration.get('complement_primary_source_sha256'))
                    or configuration.get('complement_canonical_table_sha256')!=TABLE_SHA):
                raise ValueError('Deferred primary differs from its qualified dense-source contract')
        elif configuration.get('complement_feed_sha256') != feed.fingerprint:
            raise ValueError('Report source feed differs from its native configuration')
        # Only Euler-Jacobi survivors can be false primary separators. All
        # residual factor products remain retained in the original bundle.
        factors = tuple(row for row in self.bundle.witnesses[index]
                        if (check() is None and work.left_prime < row[0] < work.right_prime and euler_jacobi(row[0])))
        return refine_report(report, configuration, primary, factors,
                             work.left_prime, work.right_prime, prime)


class PreparationProvider:
    def __init__(self, root, producer, exact_prime):
        if not callable(exact_prime):
            raise ValueError('An exact proved-prime oracle is required')
        self.root = _directory(root).resolve()
        self.plans = _directory(self.root / 'plans')
        self.queue = PreparationQueue(self.root / 'preparation')
        self.retention = PreparationRetention(self.root)
        self.producer = Path(producer).resolve(strict=True)
        self.exact_prime = exact_prime
        with self._journal():
            self.retention.recover_locked()

    @contextmanager
    def _journal(self):
        with folder_lock(self.root):
            lock = SessionControllerLock(self.root)
            try:
                yield
            finally:
                lock.release()

    def _folder(self, identity):
        _digest(identity)
        _directory(self.plans)
        return _directory(self.plans / identity)

    def request(self, requests, **budgets):
        plan = plan_preparation(requests, self.exact_prime, **budgets)
        if not plan.selected:
            raise BufferError('No complete prime-ended work fits the preparation budget; all input remains deferred')
        payload = dict(schema='PGN_PREPARED_SOURCE_INPUT_V1', plan=plan.document(),
                       requests=[dict(work_id=work['work_id'], start=str(work['start']), end=str(work['end']))
                                 for work in requests])
        encoded = canonical(payload)
        if len(encoded) > 32768:
            raise BufferError('Prepared plan exceeds its metadata budget')
        identity = hashlib.sha256(encoded).hexdigest()
        with self._journal():
            self.retention.recover_locked()
            entries = list(islice(self.plans.iterdir(), 65))
            if len(entries) >= 64 and not (self.plans / identity).exists():
                raise BufferError('Retained preparation plan capacity reached')
            folder = self._folder(identity)
            file = folder / 'input.json'
            if file.exists():
                if regular(file, 32768) != encoded + b'\n':
                    raise ValueError('Immutable prepared source input changed')
            else:
                atomic_bytes(file, encoded + b'\n')
            job = self.queue.enqueue(plan.scopes)
            reference = canonical(dict(schema='PGN_PREPARED_SOURCE_JOB_V1', plan=identity, job=job)) + b'\n'
            file = folder / 'job.json'
            if file.exists():
                if regular(file, 32768) != reference:
                    raise ValueError('Prepared source queue binding changed')
            else:
                atomic_bytes(file, reference)
            return identity, plan

    def _input(self, identity):
        with self._journal():
            self.retention.recover_locked()
            return self._input_locked(identity)

    def _input_locked(self, identity):
        _digest(identity)
        file = self.plans / identity / 'input.json'
        retired = None if file.exists() else self.retention.record(identity)
        payload = json.loads(regular(file, 32768)) if retired is None else retired['metadata']['input']
        if (type(payload) is not dict or set(payload) != {'schema', 'plan', 'requests'}
                or payload['schema'] != 'PGN_PREPARED_SOURCE_INPUT_V1'
                or hashlib.sha256(canonical(payload)).hexdigest() != identity):
            raise ValueError('Prepared source input fingerprint changed')
        plan = _decode(payload['plan'])
        requests = payload['requests']
        if type(requests) is not list or not 1 <= len(requests) <= 64:
            raise ValueError('Original prepared work inputs are missing')
        expected = {}
        for row in requests:
            if type(row) is not dict or set(row) != {'work_id', 'start', 'end'}:
                raise ValueError('Malformed original prepared work')
            work_id = _digest(row['work_id'])
            a, b = _integer(row['start']), _integer(row['end'])
            if work_id in expected or not LOW <= a < b <= DOMAIN:
                raise ValueError('Original prepared work identity or bounds changed')
            expected[work_id] = (a, b)
        if set(expected) != {work.work_id for work in plan.selected} | set(plan.deferred):
            raise ValueError('Prepared plan lost original work')
        for work in plan.selected:
            if expected[work.work_id] != (work.start, work.end):
                raise ValueError('Prepared ownership differs from its original input')
            for prime in (work.left_prime, work.right_prime):
                decision = self.exact_prime(prime)
                if type(decision) is not bool or not decision:
                    raise ValueError('Prepared coverage sentinel is not a proved prime')
        reference = (json.loads(regular(self._folder(identity) / 'job.json', 32768)) if retired is None
                     else retired['metadata']['reference'])
        if (type(reference) is not dict or set(reference) != {'schema', 'plan', 'job'}
                or reference['schema'] != 'PGN_PREPARED_SOURCE_JOB_V1' or reference['plan'] != identity):
            raise ValueError('Prepared queue reference changed')
        job = _digest(reference['job'])
        if retired is None:
            if self.queue._request(job) != plan.scopes:
                raise ValueError('Prepared queue scopes differ from the prime-ended plan')
        else:
            queue_expected = dict(schema='PGN_CPU_PREPARATION_SCOPES_V1', image_sha256=PRODUCER_SHA,
                            scopes=[[str(a), str(b)] for a, b in plan.scopes])
            if (retired['metadata']['queue_request'] != queue_expected
                    or hashlib.sha256(canonical(queue_expected)).hexdigest() != job):
                raise ValueError('Retired preparation scope binding changed')
        return plan, job, expected

    def retire(self, identity):
        """After all plan consumers handed off, retain compact cache provenance."""
        with self._journal():
            self.retention.recover_locked()
            folder = self.plans / _digest(identity)
            if not (folder / 'input.json').exists():
                return self.retention.record(identity)
            plan, job, _ = self._input_locked(identity)
            shared = False
            active = list(islice(self.plans.iterdir(), 65))
            if len(active) > 64:
                raise BufferError('Active preparation plan budget exceeded; no automatic retirement')
            for other in active:
                if other.name != identity:
                    if not re.fullmatch('[0-9a-f]{64}', other.name):
                        raise ValueError('Unexpected active preparation plan')
                    if self._input_locked(other.name)[1] == job:
                        shared = True
            entries = self.queue.entries()
            entry = next((row for row in entries if row['id'] == job), None)
            if entry is not None and entry['state'] != 'ready':
                raise PreparationRecoveryRequired('Only completed producer cache may retire')
            # Shared plans retain their job. An unacknowledged queue reference
            # likewise prevents deleting source still in active use.
            delete_job = not shared and entry is None
            job_folder = self.queue._job(job)
            completed = [job_folder / ('attempt-' + str(i)) for i in range(1,MAX_ATTEMPT_RECORDS+1)
                         if (job_folder / ('attempt-' + str(i)) / 'ready.json').is_file()
                         and (job_folder / ('attempt-' + str(i)) / 'producer-receipt.json').is_file()]
            if len(completed) != 1:
                raise PreparationRecoveryRequired('Completed cache needs one unambiguous owned receipt')
            attempt = completed[0]
            # The files above are mandatory before this adoption call: retirement
            # cannot launch a hidden replacement producer.
            bundle, receipt = run_preparation(self.producer, attempt, plan.scopes,
                                              logical_cpu=0, memory_mib=128)
            if bundle.scopes != plan.scopes:
                raise ValueError('Completed cache changed its source bounds')
            metadata = dict(input=json.loads(regular(folder / 'input.json', 32768)),
                            reference=json.loads(regular(folder / 'job.json', 32768)),
                            queue_request=json.loads(regular(job_folder / 'scopes.json', 32768)),
                            execution=receipt, ready=json.loads(regular(attempt / 'ready.json', 32768)),
                            complete=json.loads(regular(attempt / 'output/COMPLETE.json', 32768)))
            result = self.retention.retire_locked(identity, job, delete_job=delete_job, metadata=metadata)
            self.retention.rotate_locked()
            return result

    def deferred(self, identity):
        plan, _, original = self._input(identity)
        return [dict(work_id=value, start=original[value][0], end=original[value][1]) for value in plan.deferred]

    def pump(self, **resources):
        return self.queue.pump(self.producer, **resources)

    def prepared(self, identity, *, logical_cpu):
        plan, job, _ = self._input(identity)
        bundle, _ = self.queue.ready(job, self.producer, logical_cpu=logical_cpu)
        if bundle.scopes != plan.scopes:
            raise ValueError('Producer outputs escape the prepared plan')
        bank = ComplementFeedBank(tuple(prepare(bundle, i, self.exact_prime) for i in range(len(plan.scopes))))
        return PreparedSourcePlan(plan, bundle, bank, self.exact_prime)

    def runtime(self, identity, worker, table, cache, architecture, *, logical_cpu):
        prepared = self.prepared(identity, logical_cpu=logical_cpu)
        runtime = ComplementRuntime(worker, table, prepared.feeds, cache, architecture)
        runtime.prepared_source = prepared
        return runtime, prepared
