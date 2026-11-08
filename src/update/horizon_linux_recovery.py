"""Linux recovery using the native UI's per-data-directory kernel lease."""
from contextlib import contextmanager
from pathlib import Path
import os
import sys
if sys.platform=='linux':
    import fcntl
import stage_component_bundle as stage
from horizon_cohort_recovery import NativeRecovery
from horizon_linux_install import LinuxCohortLifecycle
from horizon_linux_lifecycle import OwnedLinuxProcess, owned_ipc, graceful_update_drain


@contextmanager
def ui_scope_lease(data, genesis):
    stage.require(sys.platform=='linux' and stage.DIGEST.fullmatch(genesis),
                  'Linux UI recovery lease required')
    file=stage._plain_chain(Path(data)/('.horizon-ui-'+genesis+'.lock'))
    fd=os.open(file,os.O_RDWR|os.O_CREAT|os.O_CLOEXEC|os.O_NOFOLLOW,0o600)
    try:
        fcntl.flock(fd,fcntl.LOCK_EX|fcntl.LOCK_NB)
        try:yield
        finally:fcntl.flock(fd,fcntl.LOCK_UN)
    finally:os.close(fd)


class LinuxRecovery(NativeRecovery):
    lifecycle_class=LinuxCohortLifecycle

    @staticmethod
    def own_process(*args):return OwnedLinuxProcess(*args)

    @staticmethod
    def scope_lease(*args):return ui_scope_lease(*args)

    @staticmethod
    def ipc(*args):return owned_ipc(*args)

    @staticmethod
    def graceful_drain(*args):return graceful_update_drain(*args)
