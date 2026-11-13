"""Immutable component cohorts and an atomic last-known-good selector.

Authentication is performed by the supplied reference-TUF client before any
installation work. This layer never enumerates user data or stops a process.
The application lifecycle adapter must drain its exact owned jobs and verify
the candidate's startup before publishing the selector.
"""
from pathlib import Path
from contextlib import contextmanager
import hashlib
import json
import os
import re
import shutil
import sys
import uuid
import stage_component_bundle as stage
from build_component_bundle import COMPONENT_PREFIX, PRIMARY, primary_programs
from horizon_update_client import locked_cache

MARKER = b'PGN_COMPONENT_INSTALL_STORE_V1\n'

def cohort_in_use(root, files):
    """Native file users; an unavailable observation defers reclamation."""
    if sys.platform == 'win32':
        import ctypes
        from ctypes import wintypes as w
        api = ctypes.WinDLL('rstrtmgr.dll', winmode=0x800)
        class Process(ctypes.Structure):
            _fields_=[('pid',w.DWORD),('birth',w.FILETIME)]
        class Info(ctypes.Structure):
            _fields_=[('process',Process),('application',w.WCHAR*256),('service',w.WCHAR*64),
                      ('kind',w.DWORD),('status',w.DWORD),('session',w.DWORD),('restartable',w.BOOL)]
        api.RmStartSession.argtypes = [ctypes.POINTER(w.DWORD), w.DWORD, w.LPWSTR]
        api.RmRegisterResources.argtypes = [w.DWORD,w.UINT,ctypes.POINTER(w.LPCWSTR),w.UINT,ctypes.c_void_p,w.UINT,ctypes.c_void_p]
        api.RmGetList.argtypes = [w.DWORD,ctypes.POINTER(w.UINT),ctypes.POINTER(w.UINT),ctypes.c_void_p,ctypes.POINTER(w.DWORD)]
        api.RmEndSession.argtypes = [w.DWORD]
        def affected(paths):
            if not paths:return [],False
            handle=w.DWORD();key=ctypes.create_unicode_buffer(33)
            stage.require(api.RmStartSession(ctypes.byref(handle),0,key)==0,'File-use observation unavailable')
            try:
                for start in range(0,len(paths),256):
                    chunk=list(map(str,paths[start:start+256]));array=(w.LPCWSTR*len(chunk))(*chunk)
                    stage.require(api.RmRegisterResources(handle,len(chunk),array,0,None,0,None)==0,'File-use registration unavailable')
                needed=w.UINT();count=w.UINT();reboot=w.DWORD()
                result=api.RmGetList(handle,ctypes.byref(needed),ctypes.byref(count),None,ctypes.byref(reboot))
                for attempt in range(4):
                    stage.require(result in (0,234),'File-use observation unavailable (Windows error '+str(result)+')')
                    if not needed.value:return [],bool(reboot.value)
                    count=w.UINT(needed.value);rows=(Info*count.value)()
                    result=api.RmGetList(handle,ctypes.byref(needed),ctypes.byref(count),rows,ctypes.byref(reboot))
                    if result==0:return list(rows[:count.value]),bool(reboot.value)
                raise stage.StageError('File-use observation changed during cleanup')
            finally:api.RmEndSession(handle)
        exclusive=[p for p in files if p.stat().st_nlink<=1]
        rows,reboot=affected(exclusive)
        if rows or reboot:return True
        # Shared backing files remain intact when an old hardlink is removed.
        # Restart Manager reports users by file identity, so distinguish a
        # process using the retained alias from an image loaded in this cohort.
        rows,reboot=affected([p for p in files if p.stat().st_nlink>1])
        if reboot:return True
        kernel=ctypes.WinDLL('kernel32.dll',winmode=0x800)
        kernel.OpenProcess.argtypes=[w.DWORD,w.BOOL,w.DWORD];kernel.OpenProcess.restype=w.HANDLE
        kernel.CloseHandle.argtypes=[w.HANDLE]
        kernel.GetProcessTimes.argtypes=[w.HANDLE,*([ctypes.POINTER(w.FILETIME)]*4)]
        kernel.QueryFullProcessImageNameW.argtypes=[w.HANDLE,w.DWORD,w.LPWSTR,ctypes.POINTER(w.DWORD)]
        kernel.K32EnumProcessModulesEx.argtypes=[w.HANDLE,ctypes.POINTER(w.HMODULE),w.DWORD,ctypes.POINTER(w.DWORD),w.DWORD]
        kernel.K32GetModuleFileNameExW.argtypes=[w.HANDLE,w.HMODULE,w.LPWSTR,w.DWORD]
        def normalized(path):
            if path.startswith('\\\\?\\UNC\\'):path='\\\\'+path[8:]
            elif path.startswith('\\\\?\\'):path=path[4:]
            return os.path.normcase(os.path.abspath(path))
        prefix=normalized(str(root))+os.sep
        def inside(path):return normalized(path).startswith(prefix)
        for row in rows:
            handle=kernel.OpenProcess(0x410,False,row.process.pid)
            stage.require(bool(handle),'Shared-code process could not be inspected')
            try:
                created=w.FILETIME();exited=w.FILETIME();system=w.FILETIME();user=w.FILETIME()
                stage.require(kernel.GetProcessTimes(handle,ctypes.byref(created),ctypes.byref(exited),ctypes.byref(system),ctypes.byref(user))and
                    created.dwHighDateTime==row.process.birth.dwHighDateTime and created.dwLowDateTime==row.process.birth.dwLowDateTime,'Shared-code process identity changed')
                name=ctypes.create_unicode_buffer(32768);length=w.DWORD(len(name))
                stage.require(kernel.QueryFullProcessImageNameW(handle,0,name,ctypes.byref(length)),'Shared-code image path unavailable')
                if inside(name.value):return True
                size=256
                for attempt in range(4):
                    modules=(w.HMODULE*size)();needed=w.DWORD()
                    stage.require(kernel.K32EnumProcessModulesEx(handle,modules,ctypes.sizeof(modules),ctypes.byref(needed),3),'Shared-code modules unavailable')
                    if needed.value<=ctypes.sizeof(modules):break
                    size=(needed.value+ctypes.sizeof(w.HMODULE)-1)//ctypes.sizeof(w.HMODULE)
                else:raise stage.StageError('Shared-code modules changed during cleanup')
                for module in modules[:needed.value//ctypes.sizeof(w.HMODULE)]:
                    length=kernel.K32GetModuleFileNameExW(handle,module,name,len(name))
                    stage.require(0<length<len(name),'Shared-code module path unavailable')
                    if inside(name.value):return True
            finally:kernel.CloseHandle(handle)
        return False
    if sys.platform.startswith('linux'):
        prefix=str(root)+'/'
        def inside(value):
            value=re.sub(r'\\([0-7]{3})',lambda m:chr(int(m[1],8)),value)
            return value==str(root) or value.startswith(prefix)
        for process in Path('/proc').iterdir():
            if not process.name.isdecimal():continue
            try:
                if process.stat().st_uid!=os.geteuid():continue
                for name in ('exe','cwd'):
                    try:
                        if inside(os.readlink(process/name)):return True
                    except FileNotFoundError:pass
                for fd in (process/'fd').iterdir():
                    try:
                        if inside(os.readlink(fd)):return True
                    except FileNotFoundError:pass
                with (process/'maps').open(errors='surrogateescape')as stream:
                    for line in stream:
                        fields=line.rstrip('\n').split(None,5)
                        if len(fields)==6 and inside(fields[5]):return True
            except (FileNotFoundError,ProcessLookupError):continue
        return False
    raise stage.StageError('Native file-use observation unavailable')

class RecoveryRequired(stage.StageError):
    pass


def _digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def _write(path, value):
    stage._plain_chain(path)
    raw=(json.dumps(value,sort_keys=True,separators=(',',':'))+'\n').encode()
    stage.require(len(raw)<=stage.MAX_MANIFEST,'Installation receipt exceeds bound')
    temporary=path.with_name('.'+path.name+'.'+uuid.uuid4().hex+'.pending')
    try:
        with temporary.open('xb') as stream:
            stream.write(raw);stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:
        temporary.unlink(missing_ok=True)


def inventory(root):
    """Verify the complete code-only cohort, including unselected components."""
    root=stage._plain_chain(root)
    raw=stage._read_plain(root/'components.json',stage.MAX_MANIFEST)
    value=stage._json(raw)
    stage.require(value.get('schema')=='PGN_COMPONENT_MANIFEST_V1' and
                  value.get('network') in ('testnet','main'),'Invalid cohort inventory')
    components=value.get('components');stage.require(type(components)is list and 1<=len(components)<=4,'Invalid cohort components')
    primary=primary_programs(value.get('platform','windows-x86_64'))
    names,seen,total={},set(),0
    for component in components:
        identity=component['id'];stage.require(identity in COMPONENT_PREFIX and identity not in seen,'Duplicate/unknown cohort component');seen.add(identity);stage._version(component['version'])
        rows=component['files'];stage.require(type(rows)is list and 1<=len(rows)<=stage.MAX_ENTRIES,'Unbounded cohort inventory')
        for row in rows:
            relative=COMPONENT_PREFIX[identity]+row['path'];stage._code_path('payload/'+identity+'/'+relative,identity)
            stage.require(relative.casefold()not in names,'Overlapping cohort path')
            path=stage._plain_chain(root/relative)
            stage.require(path.is_relative_to(root)and path.is_file()and path.stat().st_size==row['bytes']<=stage.MAX_FILE,'Cohort file size/path differs')
            stage.require(_digest(path)==row['sha256'],'Cohort file fingerprint differs')
            names[relative.casefold()]=dict(relative=relative,file=path,component=identity,bytes=row['bytes'],sha256=row['sha256'])
            total+=row['bytes'];stage.require(total<=stage.MAX_CACHE,'Cohort byte budget exceeded')
        stage.require(primary[identity].casefold()in names,'Cohort primary is absent')
        if value.get('platform')=='linux-x86_64':
            stage.require(names[primary[identity].casefold()]['file'].stat().st_mode&0o100,'Linux primary is not executable')
    actual=set()
    for base,dirs,files in os.walk(root,followlinks=False):
        for name in dirs+files:stage.require(not stage._linked(Path(base)/name),'Linked cohort entry')
        for name in files:actual.add((Path(base)/name).relative_to(root).as_posix().casefold())
    stage.require(actual==set(names)|{'components.json'},'Undeclared cohort files')
    return value,names,stage.sha(raw),total


class CohortStore:
    def __init__(self,root,context,context_sha256):
        self.root=stage._plain_chain(root);self.context=context;self.context_sha256=context_sha256
        self.commit_warning=''
        self.retention=dict(removed=0,in_use=0)
        stage.require(stage._read_plain(self.root/'.owner',128)==MARKER,'Not an owned installation store')
        self._quota()
        # On subsequent updates the protected running code is itself a cohort
        # in this owned store. Permit exactly that verified selected code root,
        # never a wallet/data root or arbitrary nested directory. Download and
        # staging caches retain the stricter, non-overlap rule.
        selected=Path(self.current()['cohort'])
        for name in context['protected_roots']:
            protected=stage._plain_chain(name)
            if protected.is_relative_to(self.root):
                stage.require(protected==selected and protected.parent==self.root/'cohorts',
                    'Only selected immutable code may be protected inside the installation store')
            else:stage._cache_path(self.root,dict(protected_roots=[str(protected)]))

    @classmethod
    def initialize(cls,root,base,context,context_sha256):
        root=stage._cache_path(root,context);base=stage._plain_chain(base)
        value,_,digest,_=inventory(base)
        stage.require(value['network']==context['network'] and value.get('platform','windows-x86_64')==context['platform'],'Base cohort network or platform differs')
        stage.require(not root.exists(),'Installation store already exists')
        root.mkdir(mode=0o700)
        (root/'.owner').write_bytes(MARKER)
        _write(root/'current.json',dict(schema='PGN_ACTIVE_COHORT_V1',network=context['network'],
            genesis_hash=context['genesis_hash'],cohort=str(base),inventory_sha256=digest,
            sequence=context['minimum_sequence'],previous=None))
        return cls(root,context,context_sha256)

    def _quota(self,additional=0):
        # A store spans years of updates. Retention is based on live references,
        # not a lifetime byte/file count. Admission charges actual free space.
        for base,dirs,files in os.walk(self.root,followlinks=False):
            for name in dirs+files:stage.require(not stage._linked(Path(base)/name),'Linked installation-store entry')
        if additional:
            stage.require(shutil.disk_usage(self.root).free>=additional+16*1024*1024,'Insufficient installation disk space')

    def _remember_prepared(self,candidate):
        root=self.root/'prepared';root.mkdir(exist_ok=True)
        _write(root/(self.context_sha256+'.json'),dict(schema='PGN_PREPARED_REFERENCE_V1',candidate=candidate))
        return candidate

    def _retention_roots(self,current):
        roots={stage._plain_chain(current['cohort'])}
        if current.get('previous'):roots.add(stage._plain_chain(current['previous']['cohort']))
        folder=self.root/'prepared'
        if folder.exists():
            for path in folder.iterdir():
                stage.require(re.fullmatch(r'[0-9a-f]{64}\.json',path.name),'Unknown prepared reference retained')
                value=stage._json(stage._read_plain(path,stage.MAX_MANIFEST))
                stage.require(value.get('schema')=='PGN_PREPARED_REFERENCE_V1','Unknown prepared reference retained')
                candidate=value['candidate']
                if candidate['expected_current']!=current:
                    path.unlink();continue
                target=stage._plain_chain(candidate['cohort'])
                stage.require(target.parent==self.root/'cohorts'and re.fullmatch(r'[0-9a-f]{20}',target.name),'Prepared reference escaped installation store')
                if not target.exists():path.unlink();continue
                stage.require(inventory(target)[2]==candidate['inventory_sha256'],'Prepared reference changed')
                roots.add(target)
        return roots

    def _finish_retirement(self,receipt,roots):
        value=stage._json(stage._read_plain(receipt,stage.MAX_MANIFEST))
        stage.require(value.get('schema')=='PGN_COHORT_RETIREMENT_V1'and
            re.fullmatch(r'[0-9a-f]{20}',value['original'])and
            value['quarantine']=='.retired-'+receipt.stem,'Unknown retirement retained')
        original=self.root/'cohorts'/value['original'];trash=self.root/'cohorts'/value['quarantine']
        if original in roots or trash in roots:
            if trash.exists()and not original.exists():os.rename(trash,original)
            receipt.unlink();return False
        if not trash.exists():
            if not original.exists():receipt.unlink();return True
            stage.require(inventory(original)[2]==value['inventory_sha256'],'Retiring inventory changed')
            os.rename(original,trash)
        stage.require(not original.exists(),'Ambiguous retired code retained')
        try:
            remaining=self._retirement_files(trash,value)
            # Quarantine closes the old launch path. Observe again so a
            # process starting just before the rename is retained too.
            busy=cohort_in_use(trash,remaining)
        except Exception:
            # No deletion has begun. Restore the path on an unknown result,
            # including a permission change or a process-observation failure.
            if not value.get('deletion_started'):
                os.rename(trash,original);receipt.unlink()
            raise
        if busy:
            if not value.get('deletion_started'):
                os.rename(trash,original);receipt.unlink()
            return False
        value['deletion_started']=True;_write(receipt,value)
        for p in remaining:stage._plain_chain(p).unlink()
        for base,dirs,files in os.walk(trash,topdown=False):Path(base).rmdir()
        receipt.unlink();return True

    def _retirement_files(self,trash,value):
        expected={}
        for row in value['files']:
            relative=row['path']
            if relative!='components.json':stage._code_path('payload/'+row['component']+'/'+relative,row['component'])
            stage.require(relative.casefold()not in expected and stage.DIGEST.fullmatch(row['sha256'])and type(row['bytes'])is int and 0<=row['bytes']<=stage.MAX_MANIFEST+stage.MAX_FILE,'Invalid retirement file retained')
            expected[relative.casefold()]=row
        allowed_dirs={parent.as_posix()for row in expected.values()for parent in Path(row['path']).parents if parent!=Path('.')}
        remaining=[]
        for base,dirs,files in os.walk(trash,followlinks=False):
            for name in dirs:
                p=stage._plain_chain(Path(base)/name)
                stage.require(p.relative_to(trash).as_posix()in allowed_dirs,'Undeclared retired directory retained')
            for name in files:
                p=stage._plain_chain(Path(base)/name);row=expected.get(p.relative_to(trash).as_posix().casefold())
                stage.require(row is not None and p.stat().st_size==row['bytes']and _digest(p)==row['sha256'],'Changed or undeclared retired file retained')
                remaining.append(p)
        return remaining

    def _prune_retired(self,current):
        """Called with the store writer lock. A cleanup failure cannot undo a commit."""
        result=dict(removed=0,in_use=0)
        try:
            self.require_resolved_transaction(current)
            roots=self._retention_roots(current)
            journal=self.root/'retirements';journal.mkdir(exist_ok=True)
            for receipt in list(journal.iterdir()):
                stage.require(re.fullmatch(r'[0-9a-f]{32}\.json',receipt.name),'Unknown retirement journal retained')
                result['removed'if self._finish_retirement(receipt,roots)else'in_use']+=1
            cohorts=self.root/'cohorts'
            for path in list(cohorts.iterdir())if cohorts.exists()else():
                if not re.fullmatch(r'[0-9a-f]{20}',path.name)or path in roots:continue
                value,files,digest,_=inventory(path)
                stage.require(value['network']==self.context['network']and value.get('platform','windows-x86_64')==self.context['platform'],'Retiring code belongs to another network/platform')
                if cohort_in_use(path,[x['file']for x in files.values()]+[path/'components.json']):result['in_use']+=1;continue
                token=uuid.uuid4().hex;receipt=journal/(token+'.json')
                rows=[dict(path=x['relative'],component=x['component'],bytes=x['bytes'],sha256=x['sha256'])for x in files.values()]
                rows.append(dict(path='components.json',bytes=(path/'components.json').stat().st_size,sha256=digest))
                _write(receipt,dict(schema='PGN_COHORT_RETIREMENT_V1',original=path.name,quarantine='.retired-'+token,inventory_sha256=digest,files=rows))
                result['removed'if self._finish_retirement(receipt,roots)else'in_use']+=1
        except Exception as error:
            self.commit_warning+=' Old-program cleanup deferred: '+str(error)[:240]
        self.retention=result
        return result

    def current(self):
        value=stage._json(stage._read_plain(self.root/'current.json',stage.MAX_MANIFEST))
        stage.require(value.get('schema')=='PGN_ACTIVE_COHORT_V1'and value.get('network')==self.context['network']and value.get('genesis_hash')==self.context['genesis_hash'],'Selector context differs')
        _,_,digest,_=inventory(Path(value['cohort']))
        stage.require(digest==value['inventory_sha256'],'Selected cohort changed')
        return value

    def require_resolved_transaction(self,current=None):
        """Never overwrite the only recovery frame after an interrupted helper."""
        journal=self.root/'transaction.json'
        if not journal.exists():return
        value=stage._json(stage._read_plain(journal,stage.MAX_MANIFEST))
        stage.require(type(value)is dict and value.get('schema')=='PGN_COHORT_ACTIVATION_V1',
            'Unknown installation recovery journal; existing files retained')
        current=self.current()if current is None else current
        state=value.get('state');reference=None
        if self._selector_commits_transaction(value,current):return
        if state=='committed':reference=value.get('candidate')
        elif state in ('shutdown_failed','shutdown_refused','startup_failed','activation_failed','recovered_previous')and value.get('restoration_completed')is True:
            reference=value.get('before')
        if not(type(reference)is dict and
            reference.get('cohort')==current.get('cohort')and
            reference.get('inventory_sha256')==current.get('inventory_sha256')):
            raise RecoveryRequired('An interrupted installation requires recovery; its transaction and backups have been retained')

    def _selector_commits_transaction(self,transaction,current):
        """The fsynced selector is the commit point; its final receipt may fail.

        A mere matching path is insufficient. Require the complete intended
        selector, original frame, inventory, sequence and context. This cannot
        promote a prepared-only transaction or an unrelated manual selector.
        """
        if transaction.get('state')!='starting_candidate':return False
        before=transaction.get('before');candidate=transaction.get('candidate')
        if not(type(before)is dict and type(candidate)is dict and
               candidate.get('expected_current')==before and
               before.get('network')==self.context['network'] and
               before.get('genesis_hash')==self.context['genesis_hash']):return False
        previous_sequence=before.get('sequence');sequence=candidate.get('sequence')
        if not(type(previous_sequence)is int and type(sequence)is int and
               0<=previous_sequence<=sequence<2**63):return False
        if type(candidate.get('cohort'))is not str or stage._plain_chain(candidate['cohort']).parent!=self.root/'cohorts':return False
        expected=dict(schema='PGN_ACTIVE_COHORT_V1',network=self.context['network'],
            genesis_hash=self.context['genesis_hash'],cohort=candidate.get('cohort'),
            inventory_sha256=candidate.get('inventory_sha256'),sequence=sequence,
            previous={k:v for k,v in before.items()if k!='previous'})
        return current==expected

    def prepare(self,client,selected,*,base_cohort=None,verify_running_base=None):
        # Never accept a caller-created boolean "authenticated" receipt.
        stage.require(client.context==self.context and client.context_sha256==self.context_sha256,'Authentication client context differs')
        with locked_cache(self.root):self.require_resolved_transaction()
        # Hold the download-cache lock through every stage read/copy. A second
        # helper must not reclaim our authenticated source after prepare returns.
        with client.prepared(selected,discard_after=True) as receipt:
            return self._prepare_authenticated_receipt(receipt,base_cohort=base_cohort,verify_running_base=verify_running_base)

    def _prepare_authenticated_receipt(self,receipt,*,base_cohort=None,verify_running_base=None):
        stage.require(receipt['publisher_authenticated']and receipt['context_sha256']==self.context_sha256,'Authenticated preparation required')
        source=Path(receipt['stage']);plan=stage.verify_stage(source,receipt['manifest_sha256'],self.context,self.context_sha256)
        manifest=stage._json(stage._read_plain(source/'component-bundle.json',stage.MAX_MANIFEST))
        with locked_cache(self.root):
            before=self.current();self.require_resolved_transaction(before)
            base=stage._plain_chain(base_cohort if base_cohort is not None else before['cohort'])
            old,old_files,base_digest,_=inventory(base)
            stage.require(old['network']==self.context['network'] and old.get('platform','windows-x86_64')==self.context['platform'],'Running base network or platform differs')
            actual_versions={c['id']:c['version']for c in old['components']}
            for installed in self.context['installed']:
                stage.require(actual_versions.get(installed['id'])==installed['version'],'Installed versions changed since the update request')
            if base!=Path(before['cohort']):
                selected_inventory=inventory(Path(before['cohort']))[0]
                for component in selected_inventory['components']:
                    version=actual_versions.get(component['id'])
                    stage.require(version is not None and not stage._older(version,component['version']),'Running base would downgrade a selected component')
                if verify_running_base is not None:
                    # Only explicit installation can adopt an already-running
                    # portable package. Signature/stage validation has completed;
                    # verify its exact native UI identity and application genesis
                    # before changing the baseline. Downloads alone never do so.
                    verify_running_base(base)
                    journal=self.root/'transaction.json'
                    # require_resolved_transaction() above proved this journal
                    # completed. Retire it before the atomic baseline write, so a
                    # crash on either side remains retryable without fabricating
                    # an incomplete activation or overwriting recovery evidence.
                    journal.unlink(missing_ok=True)
                    before=dict(before,cohort=str(base),inventory_sha256=base_digest,previous=None)
                    _write(self.root/'current.json',before)
            chosen=set(receipt['selection']);stage.require(chosen==set(plan['selected_with_dependencies']),'Selection differs from authenticated stage')
            key=stage.sha(json.dumps([base_digest,receipt['manifest_sha256'],sorted(chosen)],separators=(',',':')).encode())
            cohorts=self.root/'cohorts';cohorts.mkdir(exist_ok=True);destination=cohorts/key[:20]
            rows={c['id']:c for c in old['components'] if c['id']not in chosen};files={f['relative']:f['file']for f in old_files.values()if f['component']not in chosen}
            for component in manifest['components']:
                identity=component['id']
                if identity not in chosen:continue
                prefix=COMPONENT_PREFIX[identity];new_rows=[]
                for f in component['files']:
                    relative='/'.join(f['path'].split('/')[2:]);stage.require(relative.startswith(prefix),'Component layout differs')
                    stage.require(relative not in files,'Component install path overlaps')
                    files[relative]=source/f['path'];new_rows.append(dict(path=relative[len(prefix):],bytes=f['bytes'],sha256=f['sha256']))
                rows[identity]=dict(id=identity,version=component['version'],files=new_rows)
            merged=dict(schema='PGN_COMPONENT_MANIFEST_V1',release_version=manifest['release_version'],network=manifest['network'],published=True,components=[rows[k]for k in sorted(rows)])
            if self.context['platform']=='linux-x86_64' or 'platform' in old:
                merged['platform']=self.context['platform']
            expected_digest=stage.sha((json.dumps(merged,sort_keys=True,separators=(',',':'))+'\n').encode())
            if destination.exists():
                _,_,digest,_=inventory(destination);stage.require(digest==expected_digest,'Existing prepared cohort differs from authenticated files')
                return self._remember_prepared(dict(schema='PGN_PREPARED_COHORT_V1',cohort=str(destination),inventory_sha256=digest,
                    expected_current=before,base_cohort=str(base),sequence=manifest['sequence'],selected=sorted(chosen),cohort_key_sha256=key))
            # Reuse code only from a verified immutable cohort in this store.
            # A user's unpacked installation or staging cache may be edited;
            # neither is allowed to share backing storage with installed code.
            reusable={}
            if base.parent==self.root/'cohorts':
                for component in merged['components']:
                    for row in component['files']:
                        relative=COMPONENT_PREFIX[component['id']]+row['path'];old_row=old_files.get(relative.casefold())
                        if old_row and old_row['sha256']==row['sha256']:reusable[relative]=old_row['file']
            ordered=list(files.items());total=sum(f.stat().st_size for name,f in ordered if name not in reusable)
            self._prune_retired(before)
            self._quota(total+stage.MAX_MANIFEST)
            stage.require(shutil.disk_usage(self.root).free>=total+16*1024*1024,'Insufficient installation disk space')
            pending=cohorts/('.pending-'+uuid.uuid4().hex);pending.mkdir()
            try:
                links_enabled=bool(reusable)
                import horizon_update_progress as progress
                for index,(relative,source_file)in enumerate(ordered):
                    progress.report('prepare',completed=index,total=len(ordered))
                    target=pending/relative;target.parent.mkdir(parents=True,exist_ok=True)
                    if links_enabled and relative in reusable:
                        try:os.link(reusable[relative],target);continue
                        except OSError:
                            # Admit the full remaining copy before allocating
                            # it; never turn unsupported hardlinks into a
                            # partially over-quota installation.
                            stage.require(not target.exists(),'Unresolved code link creation; original files retained')
                            links_enabled=False;remaining=sum(p.stat().st_size for _,p in ordered[index:])
                            self._quota(remaining+stage.MAX_MANIFEST)
                            stage.require(shutil.disk_usage(self.root).free>=remaining+16*1024*1024,'Insufficient installation disk space for independent copies')
                    shutil.copyfile(source_file,target)
                    if self.context['platform']=='linux-x86_64':
                        with target.open('rb') as stream:is_elf=stream.read(4)==b'\x7fELF'
                        if is_elf or relative=='launch-horizon.sh':target.chmod(0o755)
                progress.report('prepare',completed=len(ordered),total=len(ordered),force=True)
                _write(pending/'components.json',merged)
                _,_,digest,_=inventory(pending)
                self._quota()
                stage._publish_pending(pending,destination,lambda:False)
            finally:
                if pending.exists():stage._remove_pending(pending,cohorts)
            return self._remember_prepared(dict(schema='PGN_PREPARED_COHORT_V1',cohort=str(destination),inventory_sha256=digest,
                expected_current=before,base_cohort=str(base),sequence=manifest['sequence'],selected=sorted(chosen),cohort_key_sha256=key))

    def activate(self,candidate,drain_owned,start_and_verify,restore_previous):
        """Publish only after exact owned shutdown and verified candidate startup.

        The lifecycle callbacks own exact process identities. restore_previous
        stops a failed candidate and restarts the prior UI; it is mandatory,
        including after a startup exception or selector publication failure.
        """
        with locked_cache(self.root):
            self.commit_warning=''
            before=self.current();self.require_resolved_transaction(before)
            stage.require(before==candidate['expected_current'],'Installation changed since preparation')
            stage.require(candidate.get('base_cohort',before['cohort'])==before['cohort'],'Prepared portable base requires owned installation verification')
            path=stage._plain_chain(candidate['cohort']);stage.require(path.parent==self.root/'cohorts','Candidate is outside installation store')
            _,_,digest,_=inventory(path);stage.require(digest==candidate['inventory_sha256'],'Prepared candidate changed')
            stage.require(candidate['sequence']>=before['sequence'],'Installation sequence would roll back')
            transaction=dict(schema='PGN_COHORT_ACTIVATION_V1',state='prepared',before=before,candidate=candidate,restoration_completed=False)
            _write(self.root/'transaction.json',transaction)
            try:
                drained=bool(drain_owned())
            except BaseException:
                # A drain can fail after the UI or Core has already exited.
                # The idempotent lifecycle adapter decides whether restoration
                # is needed; it must never blindly launch a second client.
                try:
                    restore_previous(Path(before['cohort']),path)
                    transaction['restoration_completed']=True
                finally:
                    transaction['state']='shutdown_failed';_write(self.root/'transaction.json',transaction)
                raise
            if not drained:
                restore_previous(Path(before['cohort']),path)
                transaction['restoration_completed']=True
                transaction['state']='shutdown_refused';_write(self.root/'transaction.json',transaction);return False
            restored=False
            try:
                # Shutdown has already completed. A full/locked journal must
                # take the same restoration path as a failed candidate start.
                transaction['state']='starting_candidate';_write(self.root/'transaction.json',transaction)
                healthy=bool(start_and_verify(path))
                if not healthy:
                    restored=True
                    restore_previous(Path(before['cohort']),path)
                    transaction['restoration_completed']=True
                    transaction['state']='startup_failed';_write(self.root/'transaction.json',transaction);return False
                selected=dict(schema='PGN_ACTIVE_COHORT_V1',network=self.context['network'],genesis_hash=self.context['genesis_hash'],cohort=str(path),inventory_sha256=digest,sequence=candidate['sequence'],previous={k:v for k,v in before.items()if k!='previous'})
                _write(self.root/'current.json',selected)
            except BaseException:
                try:
                    if not restored:
                        restored=True
                        restore_previous(Path(before['cohort']),path)
                        transaction['restoration_completed']=True
                finally:
                    transaction['state']='activation_failed';_write(self.root/'transaction.json',transaction)
                raise
            transaction['state']='committed'
            try:_write(self.root/'transaction.json',transaction)
            except OSError:
                # Startup was verified and current.json was already fsynced.
                # Do not roll back healthy code or report an unperformed update
                # merely because this redundant final acknowledgement failed.
                if self.current()!=selected:raise
                self.commit_warning='Update installed; the final installation receipt could not be written. Free disk space or check folder permissions.'
            self._prune_retired(selected)
            return True

    def recover_previous(self,restore_and_verify):
        """Recover an interrupted transaction without losing its original frame.

        The platform adapter owns graceful process handling and exact profile
        restoration. It must explicitly confirm health before selector recovery.
        No wallet or chain file is opened by this control-store operation.
        """
        with locked_cache(self.root):
            journal=self.root/'transaction.json';raw=stage._read_plain(journal,stage.MAX_MANIFEST)
            transaction=stage._json(raw)
            stage.require(transaction.get('schema')=='PGN_COHORT_ACTIVATION_V1'and
                transaction.get('state')in ('prepared','starting_candidate','shutdown_failed',
                    'shutdown_refused','startup_failed','activation_failed','recovered_previous'),
                'Known interrupted installation required for recovery')
            before=transaction.get('before');candidate=transaction.get('candidate')
            stage.require(type(before)is dict and type(candidate)is dict and
                before.get('genesis_hash')==self.context['genesis_hash']and
                before.get('network')==self.context['network'],'Recovery network binding differs')
            previous=stage._plain_chain(before['cohort']);failed=stage._plain_chain(candidate['cohort'])
            stage.require(failed.parent==self.root/'cohorts','Recovery candidate escaped installation store')
            for path,expected in ((previous,before['inventory_sha256']),(failed,candidate['inventory_sha256'])):
                stage.require(inventory(path)[2]==expected,'Recovery code fingerprint differs')
            current=self.current()
            stage.require(candidate.get('expected_current')==before and all(type(n)is int and 0<=n<2**63
                for n in (before.get('sequence'),candidate.get('sequence'),current.get('sequence'))),
                'Recovery sequence or original selector binding differs')
            stage.require((current['cohort'],current['inventory_sha256'])in
                ((str(previous),before['inventory_sha256']),(str(failed),candidate['inventory_sha256'])),
                'Installation changed outside the interrupted transaction')
            if self._selector_commits_transaction(transaction,current):return current
            if transaction['state']=='recovered_previous'and transaction.get('restoration_completed')is True:
                stage.require(current['cohort']==str(previous)and current['inventory_sha256']==before['inventory_sha256'],
                    'Recovered selector no longer matches previous code')
                return current
            recovery_path=self.root/'recovery.json';identity=stage.sha(raw)
            recovery=None
            if recovery_path.exists():
                old=stage._json(stage._read_plain(recovery_path,stage.MAX_MANIFEST))
                if old.get('transaction_sha256')==identity:recovery=old
            if recovery is None:
                recovery=dict(schema='PGN_COHORT_RECOVERY_V1',state='restoring_runtime',transaction_sha256=identity)
                _write(recovery_path,recovery)
            if recovery.get('state')!='runtime_verified':
                stage.require(restore_and_verify(previous,failed)is True,
                    'Previous runtime recovery was not confirmed; original backups retained')
                recovery['state']='runtime_verified';_write(recovery_path,recovery)
            # A crash after the health confirmation can retry publication without
            # stopping/restarting the restored runtime for a second time.
            restored=dict(before,sequence=max(before['sequence'],current['sequence'],candidate['sequence']))
            _write(self.root/'current.json',restored)
            transaction.update(state='recovered_previous',restoration_completed=True)
            _write(journal,transaction)
            recovery['state']='completed';_write(recovery_path,recovery)
            return restored
