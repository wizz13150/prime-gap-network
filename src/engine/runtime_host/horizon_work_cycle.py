"""Bounded local audit/audit/search ordering, not network authority or rewards."""
from pathlib import Path
import copy
import json
import re
import threading
from pgn_session import SessionControllerLock, atomic_bytes
from report_commitment import canonical_bytes
from horizon_work_cycle_history import WorkCycleHistory


def identity(value):
    return bool(isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) and value != '0' * 64)


def unique_object(pairs):
    value = {}
    for key, item in pairs:
        if key in value:
            raise ValueError('Duplicate work-cycle JSON field')
        value[key] = item
    return value


class WorkCycleJournal:
    maximum_bytes = 32768
    maximum_cycles = 4
    work_fields = ('request_id', 'ticket', 'ticket_number', 'start', 'length',
                   'target_numerator', 'target_denominator')

    def __init__(self, folder, genesis):
        if not identity(genesis):
            raise ValueError('Canonical work-cycle genesis required')
        self.folder = Path(folder)
        self.folder.mkdir(parents=True, exist_ok=True)
        self.file = self.folder / 'cycles.json'
        self.lock = threading.RLock()
        self.owner = SessionControllerLock(self.folder)
        self.history = WorkCycleHistory(self.folder, genesis, self.validate, unique_object)
        try:
            self.value = dict(schema='PGN_WORK_CYCLES_V1', genesis=genesis, cycles=[])
            if self.file.exists():
                if self.file.stat().st_size > self.maximum_bytes:
                    raise ValueError('Work-cycle journal exceeds its budget')
                saved = json.loads(self.file.read_bytes(), object_pairs_hook=unique_object)
                if saved.get('genesis') != genesis:
                    raise ValueError('Work cycles belong to another chain')
                self.validate(saved)
                self.value = saved
            self.history.check_many(self.value['cycles'])
            self.load_extra_budget()
        except BaseException:
            self.owner.release()
            raise

    def close(self):
        with self.lock:
            self.owner.release()

    def __enter__(self):
        if self.owner.fd is None:
            raise RuntimeError('Work-cycle writer is closed')
        return self

    def __exit__(self, *unused):
        self.close()

    @classmethod
    def work_identity(cls, work):
        value = {key: work.get(key) for key in cls.work_fields}
        if not identity(value['request_id']) or not identity(value['ticket']):
            raise ValueError('Canonical assigned work identities required')
        for key in cls.work_fields[2:]:
            text = value[key]
            if not isinstance(text, str) or len(text) > 29 or not re.fullmatch('[1-9][0-9]*', text):
                raise ValueError('Positive bounded work number required: ' + key)
        return value

    @classmethod
    def validate(cls, value):
        if set(value) != {'schema', 'genesis', 'cycles'} or value['schema'] != 'PGN_WORK_CYCLES_V1' or not identity(value['genesis']):
            raise ValueError('Invalid work-cycle journal schema')
        cycles = value['cycles']
        if not isinstance(cycles, list) or len(cycles) > cls.maximum_cycles:
            raise ValueError('Too many retained work cycles')
        seen = set()
        consumed_audits = set()
        for cycle in cycles:
            if not {'work', 'stage', 'audits', 'hold'} <= set(cycle) or set(cycle) - {'work', 'stage', 'audits', 'hold', 'native_service', 'extra_audits'}:
                raise ValueError('Invalid work-cycle fields')
            work = cls.work_identity(cycle['work'])
            if work != cycle['work'] or work['request_id'] in seen:
                raise ValueError('Repeated or noncanonical work cycle')
            seen.add(work['request_id'])
            if cycle['stage'] not in ('prepared', 'running', 'completed') or not isinstance(cycle['hold'], str) or len(cycle['hold']) > 240:
                raise ValueError('Invalid work-cycle stage')
            audits = cycle['audits']
            native=cycle.get('native_service')
            if 'native_service'in cycle:
                from horizon_native_work_duty import tasks
                if audits!=tasks(native,work):
                    raise ValueError('Work cycle differs from its confirmed native audit credits')
            if not isinstance(audits, list) or len(audits) != 2:
                raise ValueError('Two explicit audit stages required')
            extra = cycle.get('extra_audits', [])
            if (not isinstance(extra, list) or len(extra) > 4
                    or any(not identity(item) for item in extra) or len(set(extra)) != len(extra)):
                raise ValueError('Invalid bounded whole-audit execution budget')
            reports = set()
            for task in audits:
                if task is None:
                    continue
                if not isinstance(task, dict):
                    raise ValueError('Invalid audit task')
                if task.get('stage') == 'unavailable':
                    if set(task) != {'stage', 'anchor', 'reason'} or not identity(task['anchor']) or task['reason'] != 'no_eligible_independent_work':
                        raise ValueError('Only explicit absence of eligible work permits bootstrap')
                    continue
                fields = {'stage', 'request', 'slot', 'attempt'}
                if task.get('stage') == 'committed':
                    fields.add('block')
                    if not identity(task.get('block')):
                        raise ValueError('Audit publication needs an observed block')
                elif task.get('stage') != 'assigned':
                    raise ValueError('Invalid audit service stage')
                if set(task) != fields or not identity(task['request']) or task['request'] == work['request_id'] or task['request'] in reports:
                    raise ValueError('A cycle needs audits of distinct foreign reports')
                if type(task['slot']) is not int or task['slot'] not in (0, 1) or type(task['attempt']) is not int or not 0 <= task['attempt'] < 2**32:
                    raise ValueError('Invalid audit attempt')
                reports.add(task['request'])
                reference = (task['request'], task['slot'], task['attempt'])
                if native is None and reference in consumed_audits:
                    raise ValueError('An audit cannot serve two retained work cycles')
                if native is None:consumed_audits.add(reference)
            if cycle['stage'] != 'prepared' and not all(task and task['stage'] in ('committed', 'unavailable') for task in audits):
                raise ValueError('Search cannot precede its audit service stages')

    @staticmethod
    def ready(cycle):
        return not cycle['hold'] and all(task and task['stage'] in ('committed', 'unavailable') for task in cycle['audits'])

    def update(self, operation, request=None):
        with self.lock:
            if self.owner.fd is None:
                raise RuntimeError('Work-cycle writer is closed')
            candidate = copy.deepcopy(self.value)
            cold = request is not None and not any(cycle['work']['request_id'] == request for cycle in candidate['cycles'])
            if cold:
                saved = self.history.get(request)
                if saved is None:
                    raise ValueError('Unknown assigned work cycle')
                candidate['cycles'] = [saved]
            result = operation(candidate['cycles'])
            self.validate(candidate)
            self.history.check_many(candidate['cycles'])
            if cold:
                self.history.put(candidate['cycles'][0])
                return result
            raw = canonical_bytes(candidate) + b'\n'
            if len(raw) > self.maximum_bytes:
                raise BufferError('Work-cycle journal capacity reached')
            atomic_bytes(self.file, raw)
            self.value = candidate
            return result

    @staticmethod
    def find(cycles, request):
        for cycle in cycles:
            if cycle['work']['request_id'] == request:
                return cycle
        raise ValueError('Unknown assigned work cycle')

    def prepare(self, work, *, native_service=None):
        if work.get('genesis') != self.value['genesis']:
            raise ValueError('Assigned work belongs to another chain')
        work = self.work_identity(work)
        native_tasks=None
        if native_service is not None:
            from horizon_native_work_duty import context,tasks
            native_service=context(native_service,work);native_tasks=tasks(native_service,work)
        def apply(cycles):
            saved = self.history.get(work['request_id'])
            if saved is not None:
                if saved['work'] != work:
                    raise ValueError('Archived assigned work changed during recovery')
                if not any(cycle['work']['request_id'] == work['request_id'] for cycle in cycles):
                    return False
            for cycle in cycles:
                if cycle['work']['request_id'] == work['request_id']:
                    if cycle['work'] != work:
                        raise ValueError('Assigned work changed during recovery')
                    if cycle.get('native_service')!=native_service:
                        if native_service is None or 'native_service'not in cycle:
                            raise ValueError('Work cycle cannot silently change its service-accounting mode')
                        # Caller has re-observed the owning Core and verified
                        # the prior issuance anchor is orphaned. The immutable
                        # scientific work identity above must remain unchanged.
                        cycle['native_service']=copy.deepcopy(native_service)
                        cycle['audits']=copy.deepcopy(native_tasks)
                        if cycle['hold']==self.reorg_hold:cycle['hold']=''
                        return True
                    return False
            while len(cycles) >= self.maximum_cycles:
                old = next((item for item in cycles if item['stage'] == 'completed' and not item['hold']), None)
                if old is None:
                    raise BufferError('Retained work cycles need reconciliation first')
                self.history.put(old)
                cycles.remove(old)
            cycle=dict(work=work,stage='prepared',audits=native_tasks if native_tasks is not None else[None,None],hold='')
            if native_service is not None:cycle['native_service']=copy.deepcopy(native_service)
            cycles.append(cycle)
            return True
        return self.update(apply,work['request_id']if self.contains(work['request_id'])else None)

    permission_holds = frozenset('Audit permission pending: ' + code for code in
                                 ('auditor_not_registered', 'audit_permission_disabled'))

    def attach(self, request, position, task, *, permission_verified=False):
        if type(position) is not int or position not in (0, 1):
            raise ValueError('Audit stage zero or one required')
        if type(permission_verified) is not bool:
            raise ValueError('Explicit permission verification flag required')
        def apply(cycles):
            cycle = self.find(cycles, request)
            if cycle['stage'] != 'prepared':
                raise ValueError('Cannot retag a started search')
            if cycle['hold']:
                if not permission_verified or cycle['hold'] not in self.permission_holds:
                    raise RuntimeError('Retained work needs explicit reconciliation')
                cycle['hold'] = ''
            previous = cycle['audits'][position]
            if previous is not None and previous != task:
                raise ValueError('Audit stage already bound; reconcile it explicitly')
            cycle['audits'][position] = copy.deepcopy(task)
        self.update(apply, request)

    def published(self, request, position, audit_request, slot, attempt, block):
        if type(position) is not int or position not in (0, 1):
            raise ValueError('Audit stage zero or one required')
        def apply(cycles):
            cycle = self.find(cycles, request)
            task = cycle['audits'][position]
            if not task or (task.get('request'), task.get('slot'), task.get('attempt')) != (audit_request, slot, attempt):
                raise ValueError('Audit publication belongs to another bound attempt')
            if task['stage'] == 'committed' and task['block'] != block:
                raise ValueError('Audit publication changed branch; reconciliation required')
            task.update(stage='committed', block=block)
        self.update(apply, request)

    reorg_hold = 'Audit confirmation changed branch; retained work needs reconciliation'

    def reopen_audit(self, request, position, audit_request, slot, attempt, previous_block):
        """Reopen an exact orphaned attempt after the caller verifies Core's chain.

        This is local service accounting, not permission to replace a chain
        verdict. Started/completed ranges retain their state and both previous
        confirmation references under the existing fail-closed hold.
        """
        if type(position) is not int or position not in (0, 1) or not identity(previous_block):
            raise ValueError('Exact orphaned audit reference required')
        def apply(cycles):
            cycle = self.find(cycles, request)
            if cycle['hold'] not in ('', self.reorg_hold):
                raise RuntimeError('Unrelated retained hold cannot be cleared by an audit replay')
            task = cycle['audits'][position]
            if not task or (task.get('request'), task.get('slot'), task.get('attempt')) != (audit_request, slot, attempt):
                raise ValueError('Reassigned audit differs from the retained attempt')
            if task['stage'] == 'committed':
                if task['block'] != previous_block:
                    raise ValueError('Orphaned confirmation differs from the retained block')
                if cycle['stage'] != 'prepared':
                    cycle['hold'] = self.reorg_hold
                    return True
                task['stage'] = 'assigned'
                del task['block']
            elif task['stage'] != 'assigned':
                raise ValueError('Only an exact assigned/committed attempt can be replayed')
            cycle['hold'] = ''
            return False
        return self.update(apply, request)

    def replace_audit_confirmation(self, request, position, audit_request, slot, attempt, previous_block, block):
        if type(position) is not int or position not in (0, 1) or not identity(previous_block) or not identity(block):
            raise ValueError('Exact old and confirmed replacement audit blocks required')
        def apply(cycles):
            cycle = self.find(cycles, request)
            if cycle['stage'] == 'prepared' or cycle['hold'] != self.reorg_hold:
                raise RuntimeError('Only a held started cycle can replace its audit confirmation')
            task = cycle['audits'][position]
            if not task or task.get('stage') != 'committed' or (task.get('request'), task.get('slot'), task.get('attempt')) != (audit_request, slot, attempt):
                raise ValueError('Replacement differs from the retained audit attempt')
            if task['block'] == block:
                return False # Core confirmation persisted before a crash.
            if task['block'] != previous_block:
                raise ValueError('Replacement expected a different previous confirmation')
            task['block'] = block
            return True
        return self.update(apply, request)

    def release_reorg_hold(self, request, expected_audits):
        def apply(cycles):
            cycle = self.find(cycles, request)
            if cycle['hold'] != self.reorg_hold or cycle['audits'] != expected_audits:
                return False
            if not all(task and task['stage'] in ('committed', 'unavailable') for task in cycle['audits']):
                return False
            cycle['hold'] = ''
            return True
        return self.update(apply, request)

    def reorg_held(self):
        with self.lock:
            hot = self.value['cycles']
            return ([cycle['work']['request_id'] for cycle in hot if cycle['hold'] == self.reorg_hold]
                    + self.history.held([cycle['work']['request_id'] for cycle in hot], self.reorg_hold))

    def hold(self, request, reason):
        if not isinstance(reason, str) or not 0 < len(reason) <= 240:
            raise ValueError('Bounded reconciliation reason required')
        self.update(lambda cycles: self.find(cycles, request).__setitem__('hold', reason), request)

    def hold_permission(self, request, code):
        reason = 'Audit permission pending: ' + code
        if reason not in self.permission_holds:
            raise ValueError('Recognized Core permission refusal required')
        def apply(cycles):
            cycle = self.find(cycles, request)
            if cycle['stage'] != 'prepared' or cycle['hold'] and cycle['hold'] not in self.permission_holds:
                raise RuntimeError('Retained work needs explicit reconciliation')
            cycle['hold'] = reason
        self.update(apply, request)

    def may_audit(self, request, position):
        if type(position) is not int or position not in (0, 1):
            raise ValueError('Audit stage zero or one required')
        cycle = self.snapshot(request)
        return (cycle['stage'] == 'prepared' and not cycle['hold']
                and cycle['audits'][position] is not None
                and cycle['audits'][position]['stage'] == 'assigned'
                and all(task and task['stage'] in ('committed', 'unavailable') for task in cycle['audits'][:position]))

    def may_search(self, request):
        cycle = self.snapshot(request)
        return cycle['stage'] == 'prepared' and self.ready(cycle)

    def extra_audit_cycle(self, audit_id):
        # Hot cycles retain every unfinished search. A partition/resume of an
        # already admitted whole audit keeps its original cycle and allowance.
        with self.lock:
            return next((cycle['work']['request_id'] for cycle in self.value['cycles']
                         if audit_id in self.extra.get(cycle['work']['request_id'], [])), None)

    def load_extra_budget(self):
        # Scheduling metadata stays outside the cycle wire format. Released
        # engines can therefore read their journals after an installer rollback.
        self.extra_file = self.folder / 'audit-execution.json'
        budget = {}
        if self.extra_file.exists():
            if self.extra_file.stat().st_size > self.maximum_bytes:
                raise ValueError('Audit execution budget exceeds its storage limit')
            saved = json.loads(self.extra_file.read_bytes(), object_pairs_hook=unique_object)
            if (set(saved) != {'schema', 'genesis', 'cycles'}
                    or saved['schema'] != 'PGN_AUDIT_EXECUTION_BUDGET_V1'
                    or saved['genesis'] != self.value['genesis']
                    or not isinstance(saved['cycles'], dict) or len(saved['cycles']) > self.maximum_cycles):
                raise ValueError('Invalid audit execution budget')
            budget = saved['cycles']
            for request, audits in budget.items():
                if (not identity(request) or not isinstance(audits, list) or len(audits) > 32
                        or any(not identity(item) for item in audits) or len(set(audits)) != len(audits)):
                    raise ValueError('Invalid whole-audit execution identity')
        hot = {cycle['work']['request_id'] for cycle in self.value['cycles']}
        candidate = {key: value for key, value in budget.items() if key in hot}
        legacy = any('extra_audits' in cycle for cycle in self.value['cycles'])
        for cycle in self.value['cycles']:
            if 'extra_audits' in cycle:
                request = cycle['work']['request_id']
                candidate[request] = list(dict.fromkeys(candidate.get(request, []) + cycle['extra_audits']))
        if legacy or candidate != budget:
            # Persist the identities first. A crash before the cycle rewrite
            # replays an idempotent migration rather than spending twice.
            self.save_extra_budget(candidate)
        if legacy:
            clean = copy.deepcopy(self.value)
            for cycle in clean['cycles']:
                cycle.pop('extra_audits', None)
            atomic_bytes(self.file, canonical_bytes(clean) + b'\n')
            self.value = clean
        self.extra = candidate

    def save_extra_budget(self, budget):
        if (len(budget) > self.maximum_cycles or any(not identity(key) or len(value) > 32
                or any(not identity(item) for item in value) or len(set(value)) != len(value)
                for key, value in budget.items())):
            raise ValueError('Invalid bounded audit execution budget')
        raw = canonical_bytes(dict(schema='PGN_AUDIT_EXECUTION_BUDGET_V1',
                                   genesis=self.value['genesis'], cycles=budget)) + b'\n'
        if len(raw) > self.maximum_bytes:
            raise BufferError('Audit execution budget exceeds its storage limit')
        atomic_bytes(self.extra_file, raw)

    def extra_audits(self, request):
        with self.lock:
            return list(self.extra.get(request, []))

    @staticmethod
    def extra_audit_available(cycle, audit_id, admitted):
        # Explicitly empty regular slots may be filled by the lab lanes. They
        # occupy the first two visual audit sections, rather than catch-up.
        regular = sum(bool(task and task['stage'] == 'unavailable') for task in cycle['audits'])
        return audit_id in admitted or len(admitted) < 2 + regular

    def may_extra_audit(self, request, audit_id):
        if not identity(audit_id):
            raise ValueError('Whole-audit identity required')
        with self.lock:
            return self.extra_audit_available(self.snapshot(request), audit_id, self.extra.get(request, []))

    def reserve_extra_audit(self, request, audit_id):
        if not identity(audit_id):
            raise ValueError('Whole-audit identity required')
        with self.lock:
            if self.owner.fd is None:
                raise RuntimeError('Work-cycle writer is closed')
            cycle = self.find(self.value['cycles'], request)
            if audit_id in self.extra.get(request, []):
                return # No journal rewrite between this audit's partitions.
            if not self.extra_audit_available(cycle, audit_id, self.extra.get(request, [])):
                raise BufferError('Two catch-up audits already admitted for this ticket')
            hot = {item['work']['request_id'] for item in self.value['cycles']}
            candidate = {key: list(value) for key, value in self.extra.items() if key in hot}
            candidate.setdefault(request, []).append(audit_id)
            self.save_extra_budget(candidate)
            self.extra = candidate

    def advance(self, request, stage):
        def apply(cycles):
            cycle = self.find(cycles, request)
            permitted = {'prepared': 'running', 'running': 'completed'}
            if stage != cycle['stage'] and permitted.get(cycle['stage']) != stage:
                raise ValueError('Invalid work-cycle transition')
            if not self.ready(cycle):
                raise ValueError('Audit service is still pending')
            cycle['stage'] = stage
        self.update(apply, request)

    def snapshot(self, request):
        with self.lock:
            if not any(cycle['work']['request_id'] == request for cycle in self.value['cycles']):
                saved = self.history.get(request)
                if saved is not None:
                    return saved
            return copy.deepcopy(self.find(self.value['cycles'], request))

    def contains(self, request):
        with self.lock:
            return (any(cycle['work']['request_id'] == request for cycle in self.value['cycles'])
                    or self.history.get(request) is not None)

    def retire_assignment(self, request, position, audit_request, slot, attempt):
        if type(position) is not int or position not in (0,1):
            raise ValueError('Exact local audit position required')
        def apply(cycles):
            cycle=self.find(cycles,request)
            if 'native_service' in cycle:
                return False # Core owns these immutable consumed credits.
            task=cycle['audits'][position]
            if not task or (task.get('request'),task.get('slot'),task.get('attempt'))!=(audit_request,slot,attempt):
                raise ValueError('Retirement differs from the retained audit assignment')
            if cycle['stage']=='prepared' and task['stage']=='assigned':
                cycle['audits'][position]=None
                return True
            if not cycle['hold']:
                cycle['hold']=self.reorg_hold
            return False # Started computation/service is never retagged.
        return self.update(apply,request)

    def pending(self, position, *, retry_permissions=False):
        if type(position) is not int or position not in (0, 1):
            raise ValueError('Audit stage zero or one required')
        with self.lock:
            return next((cycle['work']['request_id'] for cycle in self.value['cycles']
                         if cycle['stage'] == 'prepared'
                         and (not cycle['hold'] or retry_permissions and cycle['hold'] in self.permission_holds)
                         and cycle['audits'][position] is None), None)

    def bound_audit(self, request, slot, attempt, position):
        with self.lock:
            for cycle in self.value['cycles']:
                if 'native_service'in cycle:continue
                task = cycle['audits'][position]
                if task and (task.get('request'), task.get('slot'), task.get('attempt')) == (request, slot, attempt):
                    return cycle['work']['request_id']
            return self.history.bound(request, slot, attempt, position)
