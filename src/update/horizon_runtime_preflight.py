"""Start authenticated code in non-working modes before touching the live UI.

This is a loader check, not a mining/network health claim. The complete cohort
inventory is verified first; no caller-supplied command is accepted here.
"""
import os
from pathlib import Path
import subprocess
import time
from horizon_component_install import inventory
import stage_component_bundle as stage


def candidate_runtime_preflight(cohort):
    cohort=Path(cohort)
    value,files,_,_=inventory(cohort)
    suffix='.exe' if value.get('platform','windows-x86_64')=='windows-x86_64' else ''
    environment=dict(os.environ,PYINSTALLER_RESET_ENVIRONMENT='1')
    environment.pop('PGN_PRIVATE_OWNER_BOOTSTRAP',None)
    checks=[]
    for relative,argument in (
        ('EngineRuntime/network/horizon-engine-host'+suffix,'--help'),
        ('UpdateRuntime/horizon-updater'+suffix,'--self-test'),
    ):
        stage.require(relative.casefold()in files,'Required runtime loader is missing')
        executable=cohort/relative
        started=time.monotonic()
        try:
            result=subprocess.run([str(executable),argument],cwd=str(cohort),
                env=environment,stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,timeout=20,check=False,
                creationflags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)if os.name=='nt'else 0)
        except (OSError,subprocess.TimeoutExpired)as error:
            raise stage.StageError('Candidate runtime cannot start before update: '+relative+' ('+type(error).__name__+')')from error
        stage.require(result.returncode==0,'Candidate runtime loader failed before update: '+relative+' (exit '+str(result.returncode)+')')
        checks.append(dict(path=relative,sha256=files[relative.casefold()]['sha256'],
            mode=argument,exit_code=0,seconds=round(time.monotonic()-started,3)))
    return dict(schema='PGN_CANDIDATE_RUNTIME_PREFLIGHT_V1',PASS=True,checks=checks,
        mining_started=False,live_processes_stopped=False)
