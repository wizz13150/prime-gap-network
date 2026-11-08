"""Bounded JSON interface for the optional component updater.

Repository addresses and initial trust roots are build-pinned. They cannot be
selected by a downloaded catalogue or by the JSON request. No shell commands,
private publisher keys or wallet files are accepted by this interface.
"""
import argparse
import json
import os
from pathlib import Path
import sys
if not getattr(sys,'frozen',False):
    sys.path.insert(0,str(Path(__file__).resolve().parents[2]/'tools'))
import stage_component_bundle as stage
from horizon_update_bootstrap import REPOSITORIES
from horizon_update_client import AuthenticatedUpdates
from horizon_component_install import CohortStore, _write
import horizon_update_progress as progress

VERSION='2026.11.8.10'


def run(request,*,repositories=REPOSITORIES,fetcher=None,lifecycle_factory=None,recovery_factory=None):
    stage.require(type(request)is dict,'JSON request object required')
    action=request.get('action')
    stage.require(action in ('check','prepare','prepare_install','install','recover'),'Unknown update operation')
    fields='schema action context cache'
    if action!='recover':fields+=' selected'
    if action=='recover':fields+=' installation_store'
    if action in ('prepare_install','install'):fields+=' base_cohort installation_store'
    if action=='install':fields+=' activation'
    stage._keys(request,fields)
    stage.require(request['schema']=='PGN_UPDATE_REQUEST_V1','Wrong update request schema')
    context,context_sha=stage.context_from_bytes(json.dumps(request['context'],sort_keys=True,separators=(',',':')).encode())
    repository=repositories.get(context['network'])
    stage.require(repository is not None and context['genesis_hash']==repository['genesis_hash']and context['compatibility_set']==repository['compatibility_set'],'No trusted update repository for this network/genesis/compatibility set')
    if action=='recover':
        stage.require(context['platform']=='windows-x86_64','Native recovery is qualified for Windows only')
        stage.require(type(request['installation_store'])is str and Path(request['installation_store']).is_absolute(),'Absolute installation store required')
        store=CohortStore(Path(request['installation_store']),context,context_sha)
        if recovery_factory is None:
            from horizon_windows_recovery import WindowsRecovery
            recovery_factory=WindowsRecovery
        lifecycle=None
        def restore(previous,failed):
            nonlocal lifecycle
            lifecycle=recovery_factory(store)
            return lifecycle(previous,failed)
        try:
            current=store.recover_previous(restore)
            return dict(schema='PGN_UPDATE_RECOVERY_RESULT_V1',recovery_performed=True,
                previous_inventory_verified=True,installation_committed=False,current=current)
        finally:
            if lifecycle is not None:lifecycle.close()
    selected=request['selected']
    stage.require(type(selected)is list and 1<=len(selected)<=4 and all(type(x)is str and x in stage.IDS for x in selected)and len(set(selected))==len(selected),'Choose one to four unique component IDs')
    stage.require(type(request['cache'])is str and Path(request['cache']).is_absolute(),'Absolute owned update-cache path required')
    client=AuthenticatedUpdates(Path(request['cache']),repository['root'].encode(),repository['root_sha256'],repository['metadata_url'],repository['targets_url'],context,context_sha,fetcher=fetcher)
    if action=='check':return client.refresh(selected)
    if action=='prepare':return client.prepare(selected)
    stage.require(all(type(request[k])is str and Path(request[k]).is_absolute()for k in ('base_cohort','installation_store')),'Absolute code/store paths required')
    store_path=Path(request['installation_store'])
    store=(CohortStore(store_path,context,context_sha)if store_path.exists()else CohortStore.initialize(store_path,Path(request['base_cohort']),context,context_sha))
    candidate=store.prepare(client,selected)
    if action=='install':
        stage.require(context['platform']=='windows-x86_64','Native installation is qualified for Windows only')
        if lifecycle_factory is None:
            from horizon_runtime_preflight import candidate_runtime_preflight
            candidate_runtime_preflight(Path(candidate['cohort']))
            from horizon_windows_install import WindowsCohortLifecycle
            lifecycle_factory=WindowsCohortLifecycle
        # The factory writes its lifecycle journal. Construct it only after
        # activate() owns the store lock and has preserved/refused old recovery.
        lifecycle=None
        def drain():
            nonlocal lifecycle
            progress.report('drain',force=True)
            lifecycle=lifecycle_factory(store,Path(request['base_cohort']),request['activation'])
            return lifecycle.drain()
        def start(path):
            progress.report('startup',force=True)
            return lifecycle.start_and_verify(path)
        def restore(*args):
            if lifecycle is not None:return lifecycle.restore_previous(*args)
        try:
            committed=store.activate(candidate,drain,start,restore)
            if committed:progress.report('committed',completed=1,total=1,force=True)
            return dict(schema='PGN_UPDATE_INSTALL_RESULT_V1',publisher_authenticated=True,
                installation_performed=committed,installation_committed=committed,
                selected=candidate['selected'],current=store.current(),process_control_performed=True)
        finally:
            if lifecycle is not None:lifecycle.close()
    return dict(schema='PGN_UPDATE_INSTALL_PREPARATION_V1',publisher_authenticated=True,
        candidate=candidate,installation_performed=False,process_control_performed=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--version',action='store_true')
    parser.add_argument('--self-test',action='store_true')
    parser.add_argument('--request-file',type=Path)
    parser.add_argument('--result-file',type=Path)
    args=parser.parse_args()
    if args.version:
        print(json.dumps(dict(schema='PGN_UPDATE_HELPER_VERSION_V1',version=VERSION)));return 0
    result_path=None
    try:
        if args.self_test:
            from tuf.api.metadata import Metadata
            from horizon_update_client import digest
            for identity,value in REPOSITORIES.items():
                raw=value['root'].encode();stage.require(digest(raw)==value['root_sha256'],'Build trust root differs');metadata=Metadata.from_bytes(raw)
                metadata.signed.verify_delegate('root',metadata.signed_bytes,metadata.signatures)
            result=dict(schema='PGN_UPDATE_HELPER_SELF_TEST_V1',PASS=True,version=VERSION,trusted_networks=sorted(REPOSITORIES),network_requested=False,installation_performed=False)
            if os.name=='nt':
                from horizon_windows_install import WindowsCohortLifecycle
                from horizon_windows_lifecycle import OwnedWindowsProcess
                from horizon_windows_recovery import WindowsRecovery
                result['windows_lifecycle_imported']=True
                result['windows_recovery_imported']=True
                if getattr(sys,'frozen',False):
                    import hashlib
                    executable=Path(sys.executable).resolve()
                    with executable.open('rb')as stream:identity=hashlib.file_digest(stream,'sha256').hexdigest()
                    with OwnedWindowsProcess(os.getpid(),executable,identity)as own:
                        result['native_process_identity_verified']=not own.exited()
        else:
            if args.request_file:
                path=stage._plain_chain(args.request_file)
                raw=stage._read_plain(path,stage.MAX_MANIFEST);request=stage._json(raw)
                stage.require(request.get('action')in ('install','recover'),'Detached request must be an installation or recovery')
                cache=stage._plain_chain(request['cache'])
                stage.require(path.name=='request.json' and path.parent.parent==cache/'sessions' and
                    len(path.parent.name)==32 and all(c in '0123456789abcdef'for c in path.parent.name),'Invalid owned update session path')
                stage.require(args.result_file is not None and stage._plain_chain(args.result_file)==path.with_name('result.json'),'Result must stay in the same owned update session')
                result_path=path.with_name('result.json')
                _write(result_path,dict(schema='PGN_UPDATE_INSTALL_PROGRESS_V1',state='verifying',installation_committed=False))
            else:
                stage.require(args.result_file is None,'A result file requires an owned request file')
                raw=sys.stdin.buffer.read(stage.MAX_MANIFEST+1);stage.require(len(raw)<=stage.MAX_MANIFEST,'Update request exceeds bound');request=stage._json(raw)
            def emit_progress(value):
                if result_path:_write(result_path,value)
                else:sys.stderr.write(json.dumps(value,separators=(',',':'))+'\n');sys.stderr.flush()
            progress.configure(emit_progress)
            try:result=run(request)
            finally:progress.configure(None)
        if result_path:_write(result_path,result)
        print(json.dumps(result,separators=(',',':')),flush=True);return 0
    except Exception as error:
        result=dict(schema='PGN_UPDATE_HELPER_ERROR_V1',error_type=type(error).__name__,error=str(error)[:512],installation_performed=False,installation_committed=False)
        if result_path:_write(result_path,result)
        print(json.dumps(result,separators=(',',':')),flush=True);return 1
if __name__=='__main__':raise SystemExit(main())
