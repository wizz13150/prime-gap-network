"""Linux adapter for the shared native component installer."""
import os
import re
from horizon_cohort_lifecycle import NativeCohortLifecycle
from horizon_linux_lifecycle import OwnedLinuxProcess, owned_ipc, graceful_update_drain


class LinuxCohortLifecycle(NativeCohortLifecycle):
    platform='linux-x86_64'
    activation_schema='PGN_LINUX_UPDATE_ACTIVATION_V1'
    lifecycle_schema='PGN_LINUX_COHORT_LIFECYCLE_V1'
    journal_name='linux-lifecycle.json'
    ui_relative='PGN-Horizon'
    core_relative='Core/pgn-core'

    @staticmethod
    def valid_birth(value):
        return type(value)is str and re.fullmatch(r'[0-9a-f-]{36}:\d{1,24}',value) is not None

    @staticmethod
    def own_process(*args):return OwnedLinuxProcess(*args)

    @staticmethod
    def ipc(*args):return owned_ipc(*args)

    @staticmethod
    def graceful_drain(*args):return graceful_update_drain(*args)

    @staticmethod
    def launch_options():return dict(start_new_session=True)

    @staticmethod
    def launch_environment():
        environment=dict(os.environ)
        for key in list(environment):
            if key=='SNAP' or key.startswith('SNAP_'):
                environment.pop(key,None)
        for key in ('LD_LIBRARY_PATH','QT_PLUGIN_PATH','QT_QPA_PLATFORM_PLUGIN_PATH'):
            if any('/snap/' in part or '/var/lib/snapd/' in part for part in environment.get(key,'').split(':')):
                environment.pop(key,None)
        return environment
