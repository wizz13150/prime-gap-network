"""No signing, wallets, processes or network activity in these plan tests."""
from pathlib import Path
import importlib.util
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'src/engine/runtime_host'))
spec = importlib.util.spec_from_file_location('operator_admission_tool', ROOT / 'tools/enroll_testnet_operator.py')
tool = importlib.util.module_from_spec(spec)
spec.loader.exec_module(tool)

class ReadOnlyCore:
    def __init__(self, permission=False, operator='a' * 64):
        self.genesis='74530feede52b8cda4fa19077747deededef0562844762a43cce462efd29102d'
        self.member = dict(signing_script='0014' + 'b' * 40, operator_id=operator, may_audit=permission)
        self.calls = []
    def verify(self):
        pass
    def call(self, method, args=None, **kwargs):
        self.calls.append(method)
        if method=='getblockchaininfo':return dict(chain='test')
        if method == 'validateaddress':
            return dict(isvalid=True, scriptPubKey='0014' + ('c' if args[0] == 'authority' else 'b') * 40)
        if method == 'getpgnparticipants':
            return dict(genesis=self.genesis, authority_script='0014' + 'c' * 40,
                        authority_request='d' * 64, participants=[self.member] if args else [])
        raise AssertionError('Read-only plan attempted mutation: ' + method)

class PermissionPlanTests(unittest.TestCase):
    def test_permission_change_refused_by_default(self):
        with self.assertRaisesRegex(ValueError, 'explicit policy review'):
            tool.enroll(ReadOnlyCore(), 'authority', 'participant', 'a' * 64, True)
    def test_explicit_permission_change_remains_read_only(self):
        core = ReadOnlyCore()
        result = tool.enroll(core, 'authority', 'participant', 'a' * 64, True, update_audit_permission=True)
        self.assertTrue(result['read_only'])
        self.assertFalse(result['applied'])
        self.assertFalse(result['already_present'])
        self.assertTrue(result['audit_permission_update_authorized'])
        self.assertEqual(result['binding']['body']['operator_id'], 'a' * 64)
        self.assertTrue(result['binding']['body']['may_audit'])
        self.assertNotIn('signmessage', core.calls)
    def test_identity_change_still_forbidden(self):
        with self.assertRaisesRegex(ValueError, 'explicit policy review'):
            tool.enroll(ReadOnlyCore(operator='e' * 64), 'authority', 'participant', 'a' * 64, True, update_audit_permission=True)
    def test_already_enabled_is_idempotent(self):
        result = tool.enroll(ReadOnlyCore(permission=True), 'authority', 'participant', 'a' * 64, True)
        self.assertTrue(result['already_present'])
        self.assertTrue(result['read_only'])
    def test_non_boolean_permission_refused(self):
        with self.assertRaisesRegex(ValueError, 'Malformed existing membership'):
            tool.enroll(ReadOnlyCore(permission=0), 'authority', 'participant', 'a' * 64, True, update_audit_permission=True)
    def test_new_epoch_is_explicitly_bound_without_legacy_genesis_constant(self):
        core=ReadOnlyCore(permission=True);core.genesis='0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d'
        result=tool.enroll(core,'authority','participant','a'*64,True)
        self.assertEqual(result['binding']['genesis'],core.genesis)
        self.assertNotIn('signmessage',core.calls)
    def test_mainnet_and_foreign_membership_are_refused_before_signing(self):
        class Main(ReadOnlyCore):
            def call(self,method,args=None,**kwargs):
                if method=='getblockchaininfo':return dict(chain='main')
                return super().call(method,args,**kwargs)
        core=Main(permission=True)
        with self.assertRaisesRegex(ValueError,'restricted to testnet'):tool.enroll(core,'authority','participant','a'*64,True)
        self.assertNotIn('signmessage',core.calls)

if __name__ == '__main__':
    unittest.main()
