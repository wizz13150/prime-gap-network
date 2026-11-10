from copy import deepcopy
from pathlib import Path
from types import SimpleNamespace
import sys, unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/engine/runtime_host'))
from horizon_reassigned_work import prove_reassignment


class ReassignmentProof(unittest.TestCase):
    def setUp(self):
        self.old = dict(genesis='1'*64, request_id='2'*64, ticket='3'*64,
            parent='4'*64, miner_script='0014'+'a'*40, reward_script='0014'+'b'*40,
            start='1500000', length='200000', segments=100)
        self.fresh = dict(self.old, schema='PGN_NATIVE_SCIENTIFIC_WORK1', accepted=False,
            parent='5'*64, ticket='6'*64, start='1800000', height=300,
            ticket_number='5', target_numerator='27', target_denominator='1')
        self.lease = dict(request=self.old['request_id'], state='assigned',
            miner_script=self.old['miner_script'], reward_script=self.old['reward_script'],
            start='1800000', end='2000000', issued=10, deadline=1000)
        self.view = dict(schema='PGN_WORK_VIEW_V1',kind='tickets',request_id=self.old['request_id'],
            state=dict(genesis=self.old['genesis'],bestblockhash='5'*64,height=299,
                       leases=[self.lease]),reports=[])
        self.header = dict(hash='4'*64,height=20,confirmations=-1)
        self.tip='5'*64
        self.calls=[]
        self.worker=SimpleNamespace(args=SimpleNamespace(genesis=self.old['genesis']),
                                    rpc=SimpleNamespace(call=self.call))

    def call(self, method, args=None):
        self.calls.append((method,deepcopy(args)))
        if method=='getpgnscientificwork':return deepcopy(self.fresh)
        if method=='getpgnworkview':return deepcopy(self.view)
        if method=='getblockheader':return deepcopy(self.header)
        if method=='getblockhash':return '7'*64
        if method=='getbestblockhash':return self.tip
        raise AssertionError('Unexpected mutation/RPC: '+method)

    def test_confirmed_request_can_have_a_reorganized_later_assignment(self):
        before=deepcopy(self.old)
        proof=prove_reassignment(self.worker,self.old)
        self.assertEqual(proof['work']['ticket'],'6'*64)
        self.assertEqual(proof['evidence']['reason'],'matured_assignment_reorganized')
        self.assertEqual(self.old,before)
        self.assertFalse(any(name in ('submitblock','signmessage')for name,_ in self.calls))

    def test_same_ticket_only_retargeted_is_unchanged(self):
        self.fresh.update(ticket=self.old['ticket'],start=self.old['start'])
        self.assertIsNone(prove_reassignment(self.worker,self.old))
        self.assertEqual(len(self.calls),1)

    def test_canonical_old_parent_must_never_be_replaced(self):
        self.header['confirmations']=30
        with self.assertRaisesRegex(ValueError,'Canonical original'):
            prove_reassignment(self.worker,self.old)

    def test_moving_anchor_makes_no_transition(self):
        self.tip='8'*64
        self.assertIsNone(prove_reassignment(self.worker,self.old))

    def test_another_owner_or_range_is_rejected(self):
        for key,value in [('miner_script','0014'+'c'*40),('start','1900000')]:
            with self.subTest(key=key):
                original=self.lease[key];self.lease[key]=value
                with self.assertRaises(ValueError):prove_reassignment(self.worker,self.old)
                self.lease[key]=original

    def test_confirmed_report_is_not_replaced(self):
        self.view['reports']=[{'request_id':self.old['request_id']}]
        self.assertIsNone(prove_reassignment(self.worker,self.old))

    def test_transport_error_does_not_invent_reassignment(self):
        def fail(*_):raise TimeoutError('transport')
        self.worker.rpc.call=fail
        with self.assertRaises(TimeoutError):prove_reassignment(self.worker,self.old)


if __name__=='__main__':unittest.main()
