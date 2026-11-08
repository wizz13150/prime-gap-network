"""Shared owned-process IPC and graceful drain for native code updates."""
import json
import os
import subprocess
import time

class LifecycleError(RuntimeError):
    pass


class ProcessAbsentError(LifecycleError):
    pass


def owned_ipc(ui, command):
    if command not in ('--inspect-owned', '--update-close-owned'):
        raise LifecycleError('Unsupported lifecycle operation')
    if ui.exited():
        raise LifecycleError('Owned UI already exited')
    result = subprocess.run([str(ui.executable), command, str(ui.pid)],
                            stdin=subprocess.DEVNULL, capture_output=True,
                            timeout=8, creationflags=0x08000000 if os.name=='nt' else 0)
    if result.returncode or len(result.stdout) > 131072:
        raise LifecycleError('Owned lifecycle response failed or exceeded its bound')
    try:
        value = json.loads(result.stdout)
    except (ValueError, UnicodeError) as error:
        raise LifecycleError('Invalid owned lifecycle response') from error
    if type(value) is not dict:
        raise LifecycleError('Owned lifecycle object required')
    return value


def graceful_update_drain(ui, core=None, timeout=75, *, ipc=owned_ipc):
    """Return only after both exact process handles confirm actual process exit.

    The caller must have checked the installed deployment's update protocol
    before invoking this API: older UIs do not recognize this CLI operation.
    """
    if not 1 <= timeout <= 120:
        raise LifecycleError('Bounded lifecycle deadline required')
    reply = ipc(ui, '--update-close-owned')
    if reply.get('accepted') is not True or reply.get('scope') != 'ORDERED_MINING_DRAIN_THEN_OWNED_CORE_STOP':
        raise LifecycleError('UI refused the ordered update shutdown')
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if ui.exited():
            if core is None or core.exited():
                return dict(schema='PGN_OWNED_UPDATE_DRAIN_V1', UI_exited=True,
                            Core_exited=core is not None, forced_termination=False)
        else:
            # The server can disappear between the handle check and this IPC
            # as the UI finishes its graceful shutdown. Only actual process handle
            # signals establish completion; an IPC race cannot establish it.
            try:
                state = ipc(ui, '--inspect-owned')
            except LifecycleError:
                state = {}
            error = state.get('pgn_update_shutdown_error')
            if error:
                raise LifecycleError(str(error)[:512])
        time.sleep(.25)
    raise LifecycleError('Graceful shutdown deadline exceeded; no process was killed')
