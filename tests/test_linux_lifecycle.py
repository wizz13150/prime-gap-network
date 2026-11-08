from pathlib import Path
import hashlib
import os
import shutil
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/update'))
from horizon_linux_lifecycle import OwnedLinuxProcess, LifecycleError, ProcessAbsentError


@unittest.skipUnless(sys.platform=='linux' and hasattr(os,'pidfd_open'),'Linux pidfd required')
class LinuxOwnershipTests(unittest.TestCase):
    def test_real_process_identity_refusals_and_exit_are_kernel_bound(self):
        with tempfile.TemporaryDirectory(prefix='pgn-linux-owner-') as folder:
            executable=Path(folder)/'owned-sleep'
            shutil.copyfile(Path(shutil.which('sleep')).resolve(),executable)
            executable.chmod(0o700)
            digest=hashlib.sha256(executable.read_bytes()).hexdigest()
            process=subprocess.Popen([str(executable),'2'],stdin=subprocess.DEVNULL,
                stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
            try:
                with OwnedLinuxProcess(process.pid,executable,digest) as owner:
                    self.assertFalse(owner.exited())
                    identity=owner.identity()
                    self.assertEqual(identity['pid'],process.pid)
                    self.assertEqual(identity['executable'],str(executable))
                    self.assertRegex(identity['birth'],r'^[0-9a-f-]{36}:\d+$')
                    with OwnedLinuxProcess(process.pid,executable,digest,identity['birth']) as same:
                        self.assertEqual(same.identity(),identity)
                    with self.assertRaises(ProcessAbsentError):
                        OwnedLinuxProcess(process.pid,executable,digest,'wrong-boot:0')
                    with self.assertRaises(LifecycleError):
                        OwnedLinuxProcess(process.pid,executable,'0'*64)
                    link=Path(folder)/'alias';link.symlink_to(executable)
                    with self.assertRaises(LifecycleError):
                        OwnedLinuxProcess(process.pid,link,digest)
                    other=Path(sys.executable).resolve()
                    with self.assertRaises(LifecycleError):
                        OwnedLinuxProcess(process.pid,other,hashlib.sha256(other.read_bytes()).hexdigest())
                    self.assertIsNone(process.poll())
                    process.wait(timeout=5)
                    self.assertTrue(owner.exited())
                with self.assertRaises(ProcessAbsentError):
                    OwnedLinuxProcess(process.pid,executable,digest,identity['birth'])
            finally:
                # Test-owned inert child only; production lifecycle has no kill.
                if process.poll() is None:
                    process.terminate();process.wait(timeout=5)


if __name__=='__main__':
    unittest.main()
