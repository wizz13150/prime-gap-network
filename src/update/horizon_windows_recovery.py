"""Restore the exact recorded profile/code state after an interrupted update.

Only verified owned processes are drained. The original lifecycle journal is
never overwritten. The data-directory lease excludes a concurrent GUI startup
while profile files are restored; no chain database or wallet file is touched.
"""
from contextlib import contextmanager
from pathlib import Path
import os
import stage_component_bundle as stage
from horizon_component_install import inventory
from horizon_windows_lifecycle import OwnedWindowsProcess,ProcessAbsentError,graceful_update_drain,owned_ipc
from horizon_windows_install import WindowsCohortLifecycle
from horizon_cohort_recovery import NativeRecovery, AbsentProcess


@contextmanager
def ui_scope_lease(data,genesis):
    stage.require(os.name=='nt'and stage.DIGEST.fullmatch(genesis),'Windows UI recovery lease required')
    import msvcrt
    file=stage._plain_chain(Path(data)/('.horizon-ui-'+genesis+'.lock'))
    with file.open('a+b')as stream:
        stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_NBLCK,1)
        try:yield
        finally:stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)


class WindowsRecovery(NativeRecovery):
    lifecycle_class=WindowsCohortLifecycle

    @staticmethod
    def own_process(*args):return OwnedWindowsProcess(*args)

    @staticmethod
    def scope_lease(*args):return ui_scope_lease(*args)

    @staticmethod
    def ipc(*args):return owned_ipc(*args)

    @staticmethod
    def graceful_drain(*args):return graceful_update_drain(*args)
