"""Read-only registration and permission diagnosis for normal miners."""
import json
import re
import threading
import time

IDENTITY = re.compile(r'^[0-9a-f]{64}$')

def identity(value):
    if not isinstance(value, str) or not IDENTITY.fullmatch(value) or value == '0' * 64:
        raise ValueError('Canonical network/request/operator identity required')
    return value

def address(value):
    if not isinstance(value, str) or not 1 <= len(value) <= 128 or any(ord(c) < 33 or ord(c) > 126 for c in value):
        raise ValueError('Public signing address required')
    return value

def script_for(rpc, value):
    value = rpc.call('validateaddress', [address(value)])
    script = value.get('scriptPubKey', '')
    if value.get('isvalid') is not True or not isinstance(script, str) or len(script) > 1024 or len(script) % 2 or not re.fullmatch('[0-9a-f]+', script):
        raise ValueError('Signing address is invalid for this Core network')
    return script

from horizon_network_summary import summarize
import copy

def join_status(rpc, genesis, signer):
    identity(genesis)
    signing_script = script_for(rpc, signer)
    for _ in range(3):
        members = rpc.call('getpgnparticipants', [0, 1, signer])
        metrics = rpc.call('getpgnnetworkmetrics', [2, 60, 600])
        if metrics.get('schema') != 'PGN_NETWORK_METRICS_V1':
            raise ValueError('Unsupported network metrics schema')
        if members.get('genesis') != genesis or metrics.get('genesis') != genesis:
            raise ValueError('Join status belongs to another network')
        tip = identity(members.get('bestblockhash'))
        if metrics.get('bestblockhash') != tip or rpc.call('getbestblockhash') != tip:
            continue
        rows = members.get('participants')
        if not isinstance(rows, list) or len(rows) > 1:
            raise ValueError('Invalid bounded participant lookup')
        count = metrics.get('audit_enabled_operators')
        if type(count) is not int or count < 0:
            raise ValueError('Invalid operator count')
        member = rows[0] if rows else None
        if member is not None:
            if member.get('signing_script') != signing_script or type(member.get('may_audit')) is not bool:
                raise ValueError('Participant does not match requested signing identity')
            identity(member.get('operator_id'))
        mode = members.get('admission_mode', 'registered')
        if mode not in ('registered', 'open'):
            raise ValueError('Unsupported network admission policy')
        public = mode == 'open'
        enabled = public or bool(member and member['may_audit'])
        if enabled and not public and count < 1:
            raise ValueError('Enabled participant is missing from operator count')
        # Only an enabled member proves its own group is in the global set.
        independent = max(0, count - int(bool(member and member['may_audit']))) if public else count - 1 if enabled else None
        blockers = []
        if not public:
            if member is None: blockers.append('registration_required')
            elif not enabled: blockers.append('audit_permission_required')
            if independent is not None and independent < 2: blockers.append('more_independent_registered_operators_required')
        return dict(schema='PGN_NORMAL_MINER_JOIN_STATUS_V1', genesis=genesis,
                    parent=tip, signing_address=signer, signing_script=signing_script,
                    operator_id=member['operator_id'] if member else None,
                    registered=member is not None, audit_permission=enabled,
                    admission_mode=mode,
                    normal_cycle_permission=enabled,
                    independent_registered_operators=independent,
                    required_independent_operators=2,
                    own_report_membership_quorum=independent is not None and independent >= 2,
                    operator_count_scope='registered_permissions_not_online_or_completed_checks',
                    authority_script=members.get('authority_script'),
                    authority_request=members.get('authority_request'), blockers=blockers,
                    network_summary=summarize(metrics, genesis))
    raise RuntimeError('Join status changed branches while being read; retry a fresh snapshot')


class AdmissionMonitor:
    """Cached permission diagnosis; never runs RPC from a presentation callback.

    Its transport must be separate from the computation controller's transport:
    closing this observer must not cancel an assignment or publication.
    """
    def __init__(self, rpc, genesis, signer=None, *, interval=30.0, clock=time.monotonic):
        identity(genesis)
        if not 1 <= interval <= 300:
            raise ValueError('Bounded admission observation interval required')
        self.rpc, self.genesis, self.interval, self.clock = rpc, genesis, interval, clock
        self._lock = threading.Lock()
        self._wake, self._stop = threading.Event(), threading.Event()
        self._thread = None
        self._signer, self._generation = None, 0
        self._value, self._observed = None, None
        self._state = 'identity_not_selected'
        self._error = None
        if signer is not None:
            self.select_identity(signer)

    @classmethod
    def from_worker(cls, worker):
        from horizon_core_rpc import CoreRpc
        args = worker.args
        # No shared transport, key generation, wallet load or identity guessing.
        rpc = CoreRpc(args.rpc_port, args.cookie, args.genesis, '')
        return cls(rpc, args.genesis, getattr(args, 'miner', None))

    def select_identity(self, signer):
        address(signer)
        with self._lock:
            if self._stop.is_set():
                return
            if signer == self._signer:
                return
            self._signer, self._generation = signer, self._generation + 1
            self._value, self._observed, self._error = None, None, None
            self._state = 'checking'
        self._wake.set()

    def snapshot(self):
        with self._lock:
            age = None if self._observed is None else max(0.0, self.clock() - self._observed)
            fresh = self._state == 'ready' and age is not None and age <= 3 * self.interval
            state = 'stale' if self._state == 'ready' and not fresh else self._state
            value = dict(schema='PGN_NORMAL_MINER_ADMISSION_OBSERVATION_V1', state=state,
                         available=fresh, age_seconds=age, error=self._error)
            if fresh:
                # The nested status contains no mutable transport or secrets.
                value['status'] = copy.deepcopy(self._value)
            return value

    def refresh_once(self):
        """Background/test entry only; snapshot() and identity binding do no I/O."""
        with self._lock:
            signer, generation = self._signer, self._generation
        if signer is None or self._stop.is_set():
            return
        try:
            value = join_status(self.rpc, self.genesis, signer)
            state, error = 'ready', None
        except Exception as failure:
            # An RPC error is unknown permission, never a zero audit backlog.
            value, state, error = None, 'unavailable', type(failure).__name__
        with self._lock:
            if generation != self._generation or self._stop.is_set():
                return
            self._value, self._state, self._error = value, state, error
            self._observed = self.clock() if value is not None else None

    def start(self):
        with self._lock:
            if self._thread is not None or self._stop.is_set():
                return
            self._thread = threading.Thread(target=self._run, name='normal-miner-admission', daemon=True)
            self._thread.start()

    def _run(self):
        while not self._stop.is_set():
            self._wake.clear()
            self.refresh_once()
            self._wake.wait(self.interval)

    def close(self):
        self._stop.set()
        self._wake.set()
        with self._lock:
            self._value, self._observed, self._error = None, None, None
            self._state = 'closed'
        self.rpc.cancel_all()
        thread = self._thread
        if thread is not None and thread is not threading.current_thread():
            thread.join(1.0)


def main(argv=None):
    """Public diagnosis only: this entry point cannot enroll or start computation."""
    import argparse
    from pathlib import Path
    from horizon_core_rpc import CoreRpc
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--rpc-port', required=True, type=int)
    parser.add_argument('--cookie', required=True, type=Path)
    parser.add_argument('--genesis', required=True)
    parser.add_argument('--miner', required=True)
    rpc_args = parser.parse_args(argv)
    rpc = CoreRpc(rpc_args.rpc_port, rpc_args.cookie, rpc_args.genesis, '')
    try:
        if rpc.call('getblockhash', [0]) != rpc_args.genesis:
            raise ValueError('Core genesis differs from the selected network')
        print(json.dumps(join_status(rpc, rpc_args.genesis, rpc_args.miner), separators=(',', ':')))
    finally:
        rpc.cancel_all()
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
