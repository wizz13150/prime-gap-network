"""Windows lifecycle adapter for authenticated immutable code cohorts.

Only bounded application configuration is rebound. Chain databases, wallet
contents, cookies, keys, scientific results and mining workspaces are never
copied, rewritten or deleted by this module.
"""
from pathlib import Path
import hashlib
import json
import os
import subprocess
import time
import stage_component_bundle as stage
from horizon_component_install import inventory, _write
from horizon_windows_lifecycle import OwnedWindowsProcess, ProcessAbsentError, owned_ipc, graceful_update_drain


class WindowsCohortLifecycle:
    def __init__(self, store, base, activation):
        stage._keys(activation, 'schema ui_pid ui_birth application_root data_root')
        stage.require(activation['schema']=='PGN_WINDOWS_UPDATE_ACTIVATION_V1', 'Invalid Windows activation context')
        stage.require(type(activation['ui_birth'])is str and activation['ui_birth'].isdigit() and 1<=len(activation['ui_birth'])<=24,'Exact UI creation time required')
        stage.require(all(type(activation[k])is str and len(activation[k])<=32768 and Path(activation[k]).is_absolute()for k in ('application_root','data_root')),'Absolute application/data paths required')
        self.store=store
        self.base=stage._plain_chain(base)
        _, files, self.base_digest, _=inventory(self.base)
        self.current=store.current()
        stage.require(Path(self.current['cohort'])==self.base, 'Running code is not the selected cohort')
        self.root=stage._plain_chain(activation['application_root'])
        self.data=stage._plain_chain(activation['data_root'])
        stage.require(self.data not in (self.base,self.store.root) and not self.data.is_relative_to(self.base), 'User data must be separate from code')
        self.runtime=self.data/('testnet/ui-runtime' if store.context['network']=='testnet' else 'ui-runtime')
        self.ui=OwnedWindowsProcess(activation['ui_pid'],self.base/'PGN-Horizon.exe',files['pgn-horizon.exe']['sha256'],activation['ui_birth'])
        self.core=None
        self.new_ui=None
        self.profiles={}
        self.started=False
        self.restored=False
        self.had_core=(self.runtime/'wallet-core.json').exists()
        self.owner_path=self.runtime/'wallet-owned.json'
        self.owner_original=None
        self.state=owned_ipc(self.ui,'--inspect-owned')
        stage.require(self.state.get('pgn_update_application_root')==str(self.root).replace('\\','/') and
                      self.state.get('pgn_update_data_root')==str(self.data).replace('\\','/') and
                      self.state.get('pgn_update_genesis')==store.context['genesis_hash'], 'Activation differs from the actual owned UI')
        self.explicit_root=bool(self.state.get('pgn_update_explicit_root'))
        self.minimized=bool(self.state.get('window_minimized'))
        owner=self.runtime/'wallet-owned.json'
        if owner.exists():
            raw=stage._read_plain(owner,32768);value=stage._json(raw)
            stage.require(value.get('genesis_hash')==store.context['genesis_hash'],'Owned Core belongs to another genesis')
            try:self.core=OwnedWindowsProcess(int(value['pid']),value['executable'],value['executable_sha256'],value['birth'])
            except ProcessAbsentError:pass
            self.owner_original=raw
        self.journal=self.store.root/'windows-lifecycle.json'
        self.record('prepared')

    def record(self,state,error=None):
        _write(self.journal,dict(schema='PGN_WINDOWS_COHORT_LIFECYCLE_V1',state=state,
            UI=self.ui.identity(),Core=self.core.identity() if self.core else None,
            data_root=str(self.data),application_root=str(self.root),explicit_root=self.explicit_root,minimized=self.minimized,
            before=self.current,profiles=[dict(path=str(p),text=raw.decode('utf-8'))for p,raw in self.profiles.items()],
            new_UI=self.new_ui.identity() if self.new_ui else None,error=str(error)[:512] if error else None))

    def __del__(self):
        # Also release read-only handles if constructor validation failed.
        for name in ('new_ui','core','ui'):
            process=getattr(self,name,None)
            if process:process.close()

    def drain(self):
        self.record('draining')
        graceful_update_drain(self.ui,self.core)
        self.record('drained')
        return True

    def rebind_profiles(self, candidate):
        _,old,_,_=inventory(self.base)
        _,new,_,_=inventory(candidate)
        changes=[]
        for name in ('wallet-core.json','worker-stream.json','cpu-controllers.json'):
            path=self.runtime/name
            if not path.exists():continue
            raw=stage._read_plain(path,32768);value=stage._json(raw)
            if name=='wallet-core.json':
                stage.require(value.get('genesis_hash')==self.store.context['genesis_hash'],'Core profile genesis differs')
                program=stage._plain_chain(value['executable'])
                with program.open('rb')as stream:old_sha=hashlib.file_digest(stream,'sha256').hexdigest()
                stage.require(old_sha==old['core/pgn-core.exe']['sha256'],'Core profile uses an unrelated installation')
                value['executable']=str(candidate/'Core/pgn-core.exe')
                value['executable_sha256']=new['core/pgn-core.exe']['sha256']
            else:
                def replace(item):
                    if isinstance(item,dict):return {k:replace(v)for k,v in item.items()}
                    if isinstance(item,list):return [replace(v)for v in item]
                    if isinstance(item,str):
                        p=Path(item)
                        if p.is_absolute():
                            try:relative=p.relative_to(self.base)
                            except ValueError:return item
                            # All old cohort files are code-only. External
                            # CPU/SSH deployments and workspaces stay intact.
                            key=relative.as_posix().casefold()
                            stage.require(key in old or any(k.startswith(key+'/')for k in old),'Configuration points to undeclared old code')
                            return str(candidate/relative)
                    return item
                value=replace(value)
            encoded=(json.dumps(value,indent=2)+'\n').encode()
            if encoded!=raw:
                self.profiles[path]=raw;changes.append((path,value))
        # Write-ahead backup before the first profile mutation.
        if self.owner_original is not None:
            stage.require(self.core is None or self.core.exited(),'Core ownership cannot be retired while its process is alive')
            stage.require(stage._read_plain(self.owner_path,32768)==self.owner_original,'Core ownership changed after shutdown verification')
            self.profiles[self.owner_path]=self.owner_original
        self.record('rebinding_profiles')
        if self.owner_original is not None:
            # This is an exact exited-process receipt, never a wallet file.
            # Keeping it under a new executable profile correctly triggers
            # CoreRuntime's foreign-deployment guard. Preserve it in the
            # durable rollback frame and let normal startup bind the new Core.
            self.owner_path.unlink()
        for path,value in changes:_write(path,value)

    def launch(self, candidate):
        _,files,_,_=inventory(candidate)
        exe=candidate/'PGN-Horizon.exe'
        args=[str(exe)]
        if self.explicit_root:args.append(str(self.root))
        args+=['--testnet' if self.store.context['network']=='testnet' else '--mainnet','--update-healthcheck']
        if self.minimized:args.append('--start-minimized')
        environment=dict(os.environ);environment.pop('QT_QPA_PLATFORM',None)
        process=subprocess.Popen(args,cwd=candidate,env=environment,stdin=subprocess.DEVNULL,
            stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=0x08000000|0x00000008)
        self.new_ui=OwnedWindowsProcess(process.pid,exe,files['pgn-horizon.exe']['sha256'])
        self.started=True;self.record('verifying_startup')
        deadline=time.monotonic()+60
        last_error=''
        while time.monotonic()<deadline:
            if self.new_ui.exited():raise RuntimeError('Updated UI exited before health confirmation')
            try:
                state=owned_ipc(self.new_ui,'--inspect-owned')
            except (RuntimeError,OSError,ValueError) as error:
                last_error=str(error);time.sleep(.25);continue
            if state.get('pgn_core_link_state')=='rejected':
                raise RuntimeError('Updated Core identity was refused: '+str(state.get('pgn_core_link_detail',''))[:300])
            try:
                if state.get('pgn_update_genesis')==self.store.context['genesis_hash'] and state.get('pgn_update_data_root')==str(self.data).replace('\\','/'):
                    if not self.had_core or state.get('pgn_core_link_state')=='ready':
                        self.record('healthy');return True
            except (RuntimeError,OSError,ValueError) as error:last_error=str(error)
            time.sleep(.25)
        raise RuntimeError('Updated client health was not confirmed: '+last_error[:200])

    def start_and_verify(self,candidate):
        self.rebind_profiles(candidate)
        return self.launch(candidate)

    def restore_previous(self, previous, failed):
        if self.restored:return
        self.restored=True
        self.record('restoring_previous')
        if self.new_ui and not self.new_ui.exited():
            current=self.runtime/'wallet-owned.json'
            owned_core=None
            try:
                if current.exists():
                    o=stage._json(stage._read_plain(current,32768))
                    try:owned_core=OwnedWindowsProcess(int(o['pid']),o['executable'],o['executable_sha256'],o['birth'])
                    except ProcessAbsentError:pass
                graceful_update_drain(self.new_ui,owned_core)
            finally:
                if owned_core:owned_core.close()
        self.restore_profiles()
        if self.ui.exited():self.launch(previous)
        elif self.core is not None and self.core.exited():
            raise RuntimeError('Core stopped while previous UI is still closing; no duplicate process was launched')
        self.record('previous_restored')

    def restore_profiles(self):
        for path,raw in self.profiles.items():
            # Restore precisely the bytes read before rebind, atomically.
            temporary=path.with_name(path.name+'.update-restore.pending')
            stage.require(not temporary.exists(),'Unresolved profile restoration exists')
            try:
                with temporary.open('xb')as stream:stream.write(raw);stream.flush();os.fsync(stream.fileno())
                os.replace(temporary,path)
            finally:temporary.unlink(missing_ok=True)

    def close(self):
        for process in (self.new_ui,self.core,self.ui):
            if process:process.close()
