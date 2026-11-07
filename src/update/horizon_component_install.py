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
import shutil
import uuid
import stage_component_bundle as stage
from build_component_bundle import COMPONENT_PREFIX, PRIMARY
from horizon_update_client import locked_cache

MARKER = b'PGN_COMPONENT_INSTALL_STORE_V1\n'
MAX_STORE = 1024*1024*1024

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
        stage.require(PRIMARY[identity].casefold()in names,'Cohort primary is absent')
    actual=set()
    for base,dirs,files in os.walk(root,followlinks=False):
        for name in dirs+files:stage.require(not stage._linked(Path(base)/name),'Linked cohort entry')
        for name in files:actual.add((Path(base)/name).relative_to(root).as_posix().casefold())
    stage.require(actual==set(names)|{'components.json'},'Undeclared cohort files')
    return value,names,stage.sha(raw),total


class CohortStore:
    def __init__(self,root,context,context_sha256):
        self.root=stage._plain_chain(root);self.context=context;self.context_sha256=context_sha256
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
        stage.require(value['network']==context['network'],'Base cohort network differs')
        stage.require(not root.exists(),'Installation store already exists')
        root.mkdir(mode=0o700)
        (root/'.owner').write_bytes(MARKER)
        _write(root/'current.json',dict(schema='PGN_ACTIVE_COHORT_V1',network=context['network'],
            genesis_hash=context['genesis_hash'],cohort=str(base),inventory_sha256=digest,
            sequence=context['minimum_sequence'],previous=None))
        return cls(root,context,context_sha256)

    def _quota(self,additional=0):
        total=0;count=0;seen=set()
        for base,dirs,files in os.walk(self.root,followlinks=False):
            for name in dirs+files:stage.require(not stage._linked(Path(base)/name),'Linked installation-store entry')
            for name in files:
                path=Path(base)/name;info=path.stat();count+=1
                # NTFS hardlinks share one physical code file. Filesystems
                # without stable file IDs are conservatively charged per path.
                identity=(info.st_dev,info.st_ino)if info.st_ino else str(path)
                if identity not in seen:total+=info.st_size;seen.add(identity)
                stage.require(total+additional<=MAX_STORE and count<=16384,'Installation store quota exceeded')

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
        if state=='committed':reference=value.get('candidate')
        elif state in ('shutdown_failed','shutdown_refused','startup_failed','activation_failed','recovered_previous')and value.get('restoration_completed')is True:
            reference=value.get('before')
        if not(type(reference)is dict and
            reference.get('cohort')==current.get('cohort')and
            reference.get('inventory_sha256')==current.get('inventory_sha256')):
            raise RecoveryRequired('An interrupted installation requires recovery; its transaction and backups have been retained')

    def prepare(self,client,selected):
        # Never accept a caller-created boolean "authenticated" receipt.
        stage.require(client.context==self.context and client.context_sha256==self.context_sha256,'Authentication client context differs')
        with locked_cache(self.root):self.require_resolved_transaction()
        receipt=client.prepare(selected)
        stage.require(receipt['publisher_authenticated']and receipt['context_sha256']==self.context_sha256,'Authenticated preparation required')
        source=Path(receipt['stage']);plan=stage.verify_stage(source,receipt['manifest_sha256'],self.context,self.context_sha256)
        manifest=stage._json(stage._read_plain(source/'component-bundle.json',stage.MAX_MANIFEST))
        with locked_cache(self.root):
            before=self.current();self.require_resolved_transaction(before)
            base=Path(before['cohort']);old,old_files,_,_=inventory(base)
            actual_versions={c['id']:c['version']for c in old['components']}
            for installed in self.context['installed']:
                stage.require(actual_versions.get(installed['id'])==installed['version'],'Installed versions changed since the update request')
            chosen=set(receipt['selection']);stage.require(chosen==set(plan['selected_with_dependencies']),'Selection differs from authenticated stage')
            key=stage.sha(json.dumps([before['inventory_sha256'],receipt['manifest_sha256'],sorted(chosen)],separators=(',',':')).encode())
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
            expected_digest=stage.sha((json.dumps(merged,sort_keys=True,separators=(',',':'))+'\n').encode())
            if destination.exists():
                _,_,digest,_=inventory(destination);stage.require(digest==expected_digest,'Existing prepared cohort differs from authenticated files')
                return dict(schema='PGN_PREPARED_COHORT_V1',cohort=str(destination),inventory_sha256=digest,
                    expected_current=before,sequence=manifest['sequence'],selected=sorted(chosen),cohort_key_sha256=key)
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
            self._quota(total+stage.MAX_MANIFEST)
            stage.require(shutil.disk_usage(self.root).free>=total+16*1024*1024,'Insufficient installation disk space')
            pending=cohorts/('.pending-'+uuid.uuid4().hex);pending.mkdir()
            try:
                links_enabled=bool(reusable)
                for index,(relative,source_file)in enumerate(ordered):
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
                _write(pending/'components.json',merged)
                _,_,digest,_=inventory(pending)
                self._quota()
                stage._publish_pending(pending,destination,lambda:False)
            finally:
                if pending.exists():stage._remove_pending(pending,cohorts)
            return dict(schema='PGN_PREPARED_COHORT_V1',cohort=str(destination),inventory_sha256=digest,
                expected_current=before,sequence=manifest['sequence'],selected=sorted(chosen),cohort_key_sha256=key)

    def activate(self,candidate,drain_owned,start_and_verify,restore_previous):
        """Publish only after exact owned shutdown and verified candidate startup.

        The lifecycle callbacks own exact process identities. restore_previous
        stops a failed candidate and restarts the prior UI; it is mandatory,
        including after a startup exception or selector publication failure.
        """
        with locked_cache(self.root):
            before=self.current();self.require_resolved_transaction(before)
            stage.require(before==candidate['expected_current'],'Installation changed since preparation')
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
            transaction['state']='starting_candidate';_write(self.root/'transaction.json',transaction)
            restored=False
            try:
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
            transaction['state']='committed';_write(self.root/'transaction.json',transaction)
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
