"""One actual client adapter, exact group coverage and canonical terminal ACKs."""
from pathlib import Path
from types import SimpleNamespace
import copy, sys, unittest
import types

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'src/engine/runtime_host'), str(ROOT / 'src/protocol'), str(ROOT / 'tests')]
from horizon_core_rpc import CoreRpc
from horizon_audit_snapshot import AnchoredAuditStates
from horizon_network_audits import NetworkAudits
import test_owner_report_queue as owner_fixture

G, TIP, COMMIT = 'a' * 64, 'b' * 64, 'c' * 64
def identity(number):
    return f'{number:064x}'


class Rpc(CoreRpc):
    def __init__(self, count=1):
        self.genesis, self.tip, self.canonical = G, TIP, TIP
        self.calls = []
        self.reply = dict(schema='PGN_AUDIT_ACTIVITY_V1', state=dict(genesis=G, bestblockhash=TIP, height=8),
                          truncated=True, absent_watched_requests=[], audits=[dict(request_id=identity(i+1), audited=True,
                          jobs=[dict(slot=0, attempt=0, state='passed', commitment=COMMIT),
                                dict(slot=1, attempt=0, state='passed', commitment='d'*64)]) for i in range(count)])
    def call(self, method, params=None, **kwargs):
        self.calls.append((method, params))
        if method == 'getpgnauditactivity':
            return copy.deepcopy(self.reply)
        if method == 'getblockhash':
            return self.canonical
        if method == 'getbestblockhash':
            return self.tip
        raise AssertionError('Unexpected expensive or mutating RPC: ' + method)


class AtomicAuditTests(unittest.TestCase):
    def client(self, rpc):
        client = NetworkAudits.__new__(NetworkAudits)
        client.worker = SimpleNamespace(rpc=rpc)
        return client

    def test_all_100_watched_terminal_rows_need_one_heavy_atomic_read(self):
        rpc = Rpc(100)
        client = self.client(rpc)
        states = client.states([identity(i+1) for i in range(100)])
        self.assertIsInstance(states, AnchoredAuditStates)
        self.assertEqual(len(states), 100)
        for request in states:
            self.assertTrue(client.owner_terminal_on_anchor(request, states.anchor, states))
        self.assertEqual(sum(method == 'getpgnauditactivity' for method, _ in rpc.calls), 1)
        self.assertFalse(any(method in ('getpgnworkview', 'getpgnauditstate') for method, _ in rpc.calls))
        self.assertEqual(len(rpc.calls), 102)  # One batch, one canonical height,100 cheap tip checks.

    def test_exact_own_attempt_and_commitment_are_required_for_audit_ack(self):
        rpc = Rpc()
        client = self.client(rpc)
        states = client.states([identity(1)])
        audit = dict(request_id=identity(1), slot=0, attempt=0, answer=dict(commitment=COMMIT))
        self.assertTrue(client.terminal_on_anchor(audit, states.anchor, states))
        for changed in [dict(audit, slot=1), dict(audit, attempt=1), dict(audit, answer=dict(commitment='d'*64))]:
            self.assertFalse(client.terminal_on_anchor(changed, states.anchor, states))

    def test_reorg_after_read_retains_both_owner_and_auditor_records(self):
        rpc = Rpc()
        client = self.client(rpc)
        states = client.states([identity(1)])
        rpc.tip = 'd' * 64
        self.assertFalse(client.owner_terminal_on_anchor(identity(1), states.anchor, states))
        self.assertFalse(client.terminal_on_anchor(dict(request_id=identity(1), slot=0, attempt=0,
                         answer=dict(commitment=COMMIT)), states.anchor, states))

    def test_orphaned_or_mislabelled_height_returns_no_snapshot(self):
        rpc = Rpc()
        rpc.canonical = 'd' * 64
        self.assertIsNone(rpc.audit_snapshot([identity(1)]))

    def test_explicit_absence_is_distinct_from_missing_rows(self):
        rpc = Rpc()
        rpc.reply['audits'] = []
        rpc.reply['absent_watched_requests'] = [identity(1)]
        self.assertEqual(rpc.audit_snapshot([identity(1)]), {})
        rpc.reply['absent_watched_requests'] = []
        with self.assertRaises(ValueError):
            rpc.audit_snapshot([identity(1)])

    def test_absent_watched_report_can_leave_room_for_global_activity_rows(self):
        rpc = Rpc()
        rpc.reply['audits'][0]['request_id'] = identity(2)
        rpc.reply['absent_watched_requests'] = [identity(1)]
        self.assertEqual(rpc.audit_snapshot([identity(1)]), {})

    def test_real_owner_catalogue_retires_a_terminal_row_without_per_row_workview(self):
        fixture = owner_fixture.OwnerReportQueueTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        request, item, _ = fixture.completed(1)
        fixture.queue.register(request, item)
        fixture.rpc.genesis = G
        fixture.rpc.chain[0] = G
        fixture.rpc.states[request]['jobs'] = copy.deepcopy(Rpc().reply['audits'][0]['jobs'])
        original = fixture.rpc.call
        def call(method, params=None, **kwargs):
            if method == 'getpgnauditactivity':
                fixture.rpc.calls.append((method, params))
                return dict(schema='PGN_AUDIT_ACTIVITY_V1', state=dict(genesis=G,
                    height=max(fixture.rpc.chain), bestblockhash=fixture.rpc.chain[max(fixture.rpc.chain)]),
                    absent_watched_requests=[], audits=[dict(request_id=key, **fixture.rpc.states[key]) for key in params[2]])
            return original(method, params, **kwargs)
        fixture.rpc.call = call
        fixture.rpc.audit_snapshot = types.MethodType(CoreRpc.audit_snapshot, fixture.rpc)
        fixture.client.reveal_owner_reports()
        self.assertEqual(fixture.count(waiting=1), 0)
        self.assertEqual(fixture.sent, [request])
        self.assertFalse(any(method == 'getpgnworkview' for method, _ in fixture.rpc.calls))

    def test_invalid_atomic_response_never_falls_back_to_unanchored_reads(self):
        mutations = [lambda v: v.update(schema='wrong'), lambda v: v['state'].update(genesis='d'*64),
                     lambda v: v['state'].update(height=True), lambda v: v.update(state=[]),
                     lambda v: v['audits'].append(copy.deepcopy(v['audits'][0])),
                     lambda v: v['audits'][0].update(request_id=identity(2)),
                     lambda v: v['audits'][0].update(audited=1),
                     lambda v: v['audits'][0]['jobs'][0].update(attempt=-1),
                     lambda v: v['audits'][0]['jobs'][0].update(commitment='bad'),
                     lambda v: v['audits'][0]['jobs'].append(copy.deepcopy(v['audits'][0]['jobs'][0]))]
        for mutate in mutations:
            with self.subTest(mutation=mutate):
                rpc = Rpc()
                mutate(rpc.reply)
                with self.assertRaises(ValueError):
                    self.client(rpc).states([identity(1)])
                self.assertEqual([method for method, _ in rpc.calls], ['getpgnauditactivity'])

    def test_invalid_request_groups_do_not_perform_rpc(self):
        for requests in [[], [identity(1)]*2, ['bad'], [dict(invalid=True)], [identity(i+1) for i in range(101)]]:
            rpc = Rpc()
            with self.assertRaises(ValueError):
                rpc.audit_snapshot(requests)
            self.assertEqual(rpc.calls, [])


if __name__ == '__main__':
    unittest.main()
