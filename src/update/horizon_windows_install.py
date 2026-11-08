"""Windows adapter for the shared native component installer."""
import os
from horizon_cohort_lifecycle import NativeCohortLifecycle
from horizon_windows_lifecycle import OwnedWindowsProcess, ProcessAbsentError, owned_ipc, graceful_update_drain


class WindowsCohortLifecycle(NativeCohortLifecycle):
    platform='windows-x86_64'
    activation_schema='PGN_WINDOWS_UPDATE_ACTIVATION_V1'
    lifecycle_schema='PGN_WINDOWS_COHORT_LIFECYCLE_V1'
    journal_name='windows-lifecycle.json'
    ui_relative='PGN-Horizon.exe'
    core_relative='Core/pgn-core.exe'

    @staticmethod
    def valid_birth(value):
        return type(value)is str and value.isdigit() and 1<=len(value)<=24

    @staticmethod
    def own_process(*args):return OwnedWindowsProcess(*args)

    @staticmethod
    def ipc(*args):return owned_ipc(*args)

    @staticmethod
    def graceful_drain(*args):return graceful_update_drain(*args)

    @staticmethod
    def launch_options():return dict(creationflags=0x08000000|0x00000008)

    @staticmethod
    def launch_environment():
        environment=dict(os.environ);environment.pop('QT_QPA_PLATFORM',None)
        return environment
