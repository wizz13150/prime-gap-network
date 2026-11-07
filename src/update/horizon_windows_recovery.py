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


@contextmanager
def ui_scope_lease(data,genesis):
    stage.require(os.name=='nt'and stage.DIGEST.fullmatch(genesis),'Windows UI recovery lease required')
    import msvcrt
    file=stage._plain_chain(Path(data)/('.horizon-ui-'+genesis+'.lock'))
    with file.open('a+b')as stream:
        stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_NBLCK,1)
        try:yield
        finally:stream.seek(0);msvcrt.locking(stream.fileno(),msvcrt.LK_UNLCK,1)


class AbsentProcess:
    def __init__(self,identity):self.saved=identity
    def exited(self):return True
    def close(self):pass
    def identity(self):return self.saved


class WindowsRecovery:
    def __init__(self,store):
        self.store=store;self.handles=[];self.life=None
        self.transaction=stage._json(stage._read_plain(store.root/'transaction.json',stage.MAX_MANIFEST))
        self.frame_raw=stage._read_plain(store.root/'windows-lifecycle.json',stage.MAX_MANIFEST)
        self.frame=stage._json(self.frame_raw);frame=self.frame
        stage.require(frame.get('schema')=='PGN_WINDOWS_COHORT_LIFECYCLE_V1'and
            frame.get('before')==self.transaction.get('before'),'No matching native recovery frame; original files retained')
        self.data=stage._plain_chain(frame['data_root']);self.root=stage._plain_chain(frame['application_root'])
        protected={stage._plain_chain(path)for path in store.context['protected_roots']}
        stage.require(self.data in protected,'Recovery data directory is outside the approved scope')
        self.runtime=self.data/('testnet/ui-runtime'if store.context['network']=='testnet'else 'ui-runtime')
        allowed={self.runtime/name for name in ('wallet-core.json','worker-stream.json','cpu-controllers.json','wallet-owned.json')}
        self.profiles={}
        rows=frame.get('profiles');stage.require(type(rows)is list and len(rows)<=4,'Invalid recovery profile set')
        for row in rows:
            stage._keys(row,'path text');path=stage._plain_chain(row['path'])
            stage.require(path in allowed and path not in self.profiles and type(row['text'])is str,
                'Recovery may restore only the recorded application profiles')
            raw=row['text'].encode();stage.require(len(raw)<=32768,'Recovery profile exceeds bound')
            value=stage._json(raw)
            if path.name in ('wallet-core.json','wallet-owned.json'):
                stage.require(value.get('genesis_hash')==store.context['genesis_hash'],'Recovery profile belongs to another genesis')
            self.profiles[path]=raw

    def _process(self,identity,sha256):
        stage.require(type(identity)is dict and type(identity.get('pid'))is int and
            type(identity.get('birth'))is str and identity['birth'].isdigit(),'Invalid recorded process identity')
        try:process=OwnedWindowsProcess(identity['pid'],identity['executable'],sha256,identity['birth'])
        except ProcessAbsentError:return AbsentProcess(identity)
        self.handles.append(process);return process

    def __call__(self,previous,failed):
        frame=self.frame;old=inventory(previous)[1];new=inventory(failed)[1]
        # A retry after the restored GUI became healthy must not stop it again.
        # Only a separate, matching recovery receipt can identify that GUI.
        recovery_journal=self.store.root/'recovery-windows-lifecycle.json'
        if recovery_journal.exists():
            saved=stage._json(stage._read_plain(recovery_journal,stage.MAX_MANIFEST))
            identity=saved.get('new_UI')
            if saved.get('before')==self.transaction['before']and saved.get('state')in ('healthy','previous_restored')and identity:
                stage.require(Path(identity['executable'])==previous/'PGN-Horizon.exe','Restored UI code path differs')
                restored=self._process(identity,old['pgn-horizon.exe']['sha256'])
                if not restored.exited():
                    state=owned_ipc(restored,'--inspect-owned')
                    stage.require(state.get('pgn_update_genesis')==self.store.context['genesis_hash']and
                        state.get('pgn_update_data_root')==self.data.as_posix()and
                        (not(self.runtime/'wallet-core.json').exists()or state.get('pgn_core_link_state')=='ready'),
                        'Restored UI health is not confirmed; previous recovery receipt retained')
                    return True
        stage.require(Path(frame['UI']['executable'])==previous/'PGN-Horizon.exe','Original UI code path differs')
        original=self._process(frame['UI'],old['pgn-horizon.exe']['sha256'])
        updated=None
        if frame.get('new_UI'):
            stage.require(Path(frame['new_UI']['executable'])==failed/'PGN-Horizon.exe','Updated UI code path differs')
            updated=self._process(frame['new_UI'],new['pgn-horizon.exe']['sha256'])
        live=[p for p in (original,updated)if p is not None and not p.exited()]
        stage.require(len(live)<=1,'Multiple owned interfaces are active; recovery retained')
        owner_path=self.runtime/'wallet-owned.json';owner_raw=None;core=None;cores=[]
        if frame.get('Core'):
            core=self._process(frame['Core'],old['core/pgn-core.exe']['sha256']);cores.append(core)
        if owner_path.exists():
            owner_raw=stage._read_plain(owner_path,32768);owner=stage._json(owner_raw)
            stage.require(owner.get('genesis_hash')==self.store.context['genesis_hash'],'Current Core recovery genesis differs')
            program=Path(owner['executable']);permitted={previous/'Core/pgn-core.exe':old['core/pgn-core.exe']['sha256'],failed/'Core/pgn-core.exe':new['core/pgn-core.exe']['sha256']}
            if frame.get('Core'):permitted[Path(frame['Core']['executable'])]=old['core/pgn-core.exe']['sha256']
            stage.require(permitted.get(program)==owner.get('executable_sha256'),'Current Core is outside the recorded code scope')
            core=self._process(dict(pid=int(owner['pid']),birth=str(owner['birth']),executable=owner['executable']),owner['executable_sha256'])
            cores.append(core)
        if live:graceful_update_drain(live[0],core if core and not core.exited()else None)
        stage.require(all(p.exited()for p in cores),'Close the owned Core before offline recovery; no foreign process was stopped')
        with ui_scope_lease(self.data,self.store.context['genesis_hash']):
            stage.require(stage._read_plain(self.store.root/'windows-lifecycle.json',stage.MAX_MANIFEST)==self.frame_raw,'Recovery frame changed')
            # Current ownership is disposable only after exact process death.
            # Never restore a stale PID receipt from before the update.
            if owner_raw is not None:
                stage.require(stage._read_plain(owner_path,32768)==owner_raw,'Core ownership changed during recovery')
                owner_path.unlink()
            life=WindowsCohortLifecycle.__new__(WindowsCohortLifecycle);self.life=life
            life.store=self.store;life.current=self.transaction['before'];life.base=previous
            life.data=self.data;life.root=self.root;life.runtime=self.runtime
            life.explicit_root=bool(frame.get('explicit_root'));life.minimized=bool(frame.get('minimized'))
            life.profiles={p:raw for p,raw in self.profiles.items()if p.name!='wallet-owned.json'}
            life.ui=AbsentProcess(frame['UI']);life.core=None;life.new_ui=None
            life.started=False;life.restored=False;life.had_core=(self.runtime/'wallet-core.json').exists()
            life.journal=recovery_journal
            life.record('restoring_profiles');life.restore_profiles();life.record('profiles_restored')
        # This launch verifies actual UI/genesis/data/Core readiness. It writes
        # a separate recovery journal and cannot destroy the original backups.
        return life.launch(previous)is True

    def close(self):
        if self.life:self.life.close()
        for process in self.handles:process.close()
