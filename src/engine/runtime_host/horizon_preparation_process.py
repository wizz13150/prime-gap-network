"""One owned, bounded CPU preparation; successful receipts may be adopted on restart.

This executes mathematical scopes, not network leases. The coordinator supplies
already planned scopes and a reserved logical CPU. GPU ownership is untouched.
"""
from pathlib import Path
import hashlib,json,os,time
from pgn_owned import WindowsOwnedProcess,image_hash,_job_limit_bytes
from pgn_artifact_lease import _open_read_lease,_hash
from pgn_session import SessionControllerLock,atomic_bytes
from horizon_residual_bundle import bind,native_output_argument

from horizon_scientific_identity import PRODUCER_SHA
MAX_LOG_BYTES=1024*1024


class PreparationCancelled(InterruptedError):
    """An explicit controller cancellation, distinct from an execution failure."""
    pass


class PreparationRecoveryRequired(RuntimeError):
    pass


def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode('utf-8')


def regular(path,maximum):
    path=Path(path)
    if (path.is_symlink()or getattr(path.lstat(),'st_file_attributes',0)&0x400
            or not path.is_file()or path.stat().st_size>maximum):
        raise ValueError('Preparation file exceeds its path/byte budget')
    with path.open('rb')as stream:raw=stream.read(maximum+1)
    if len(raw)>maximum:raise ValueError('Preparation file grew beyond its byte budget')
    return raw


class _CpuLease:
    def __init__(self,folder,configuration):
        self.file=folder/'producer-owner.json';self.configuration=configuration
        self.record=dict(schema='PGN_CPU_PREPARATION_OWNER_V1',role='cpu_preparation',configuration_sha256=configuration)
        self.released=False
    def bind(self,pid,birth,**identity):
        if identity.get('configuration_sha256',self.configuration)!=self.configuration:
            raise ValueError('Preparation identity differs from the immutable request')
        self.record.update(pid=pid,birth=birth,**identity)
        atomic_bytes(self.file,canonical(self.record)+b'\n')
    def release(self):
        self.released=True;self.record['state']='OWNED_PROCESS_CLOSED'
        atomic_bytes(self.file,canonical(self.record)+b'\n')


def _publish_ready(folder,configuration,bundle,receipt_bytes):
    encoded=canonical(dict(schema='PGN_CPU_PREPARATION_READY_V1',configuration_sha256=configuration,
        producer_receipt_sha256=hashlib.sha256(receipt_bytes).hexdigest(),
        complete_output_sha256=bundle.receipt_sha256,witness_sha256=list(bundle.witness_sha256),
        network_admitted=False))+b'\n'
    file=folder/'ready.json'
    if file.exists():
        if regular(file,32768)!=encoded:raise ValueError('Immutable preparation readiness changed')
    else:atomic_bytes(file,encoded)


def run_preparation(worker,folder,scopes,*,logical_cpu,memory_mib=768,timeout=1800,cancelled=None,environment=None):
    if os.name!='nt':raise ValueError('The qualified preparation executor currently requires Windows')
    _job_limit_bytes(dict(logical_cpu=logical_cpu,memory_mib=memory_mib))
    worker=Path(worker).resolve(strict=True)
    # An updater cannot replace/write this image between hashing, suspended
    # creation and execution. This independent pin permits an outer host lease.
    with _open_read_lease(worker)as pinned:
        if _hash(pinned)!=PRODUCER_SHA:raise ValueError('Unqualified preparation producer image')
        return _run_preparation(worker,folder,scopes,logical_cpu=logical_cpu,memory_mib=memory_mib,
                                timeout=timeout,cancelled=cancelled,environment=environment)


def _run_preparation(worker,folder,scopes,*,logical_cpu,memory_mib,timeout,cancelled,environment):
    if os.name!='nt':raise ValueError('The qualified preparation executor currently requires Windows')
    if (type(scopes)not in(list,tuple)or not 1<=len(scopes)<=64
            or type(timeout)not in(int,float)or not 0<timeout<=3600
            or cancelled is not None and not callable(cancelled)):
        raise ValueError('Bounded finite preparation scopes and timeout required')
    ordered=[];prior=0;total=0
    for pair in scopes:
        if type(pair)not in(list,tuple)or len(pair)!=2 or any(type(n)is not int for n in pair):
            raise ValueError('Exact preparation bounds required')
        a,b=pair
        if not 3<=a<b<=10**21 or a<prior:raise ValueError('Preparation scopes overlap or exceed the proved domain')
        prior=b;total+=b-a;ordered.append((a,b))
    if total>10**15:raise ValueError('Preparation exceeds the covered-position budget')
    if cancelled is not None and cancelled():raise PreparationCancelled('Preparation cancelled before execution')
    worker=Path(worker).resolve(strict=True)
    if image_hash(worker)!=PRODUCER_SHA:raise ValueError('Unqualified preparation producer image')
    folder=Path(folder)
    if folder.exists()and(folder.is_symlink()or getattr(folder.lstat(),'st_file_attributes',0)&0x400):
        raise ValueError('Preparation directory cannot be a reparse point')
    folder.mkdir(parents=True,exist_ok=True);folder=folder.resolve()
    output=folder/'output'
    command=[str(worker),'10000000',native_output_argument(output),*[str(n)for pair in ordered for n in pair]]
    request=dict(schema='PGN_CPU_PREPARATION_REQUEST_V1',image_sha256=PRODUCER_SHA,args=command)
    encoded=canonical(request);configuration=hashlib.sha256(encoded).hexdigest()
    lock=SessionControllerLock(folder);owned=None;lease=_CpuLease(folder,configuration);began=time.monotonic()
    try:
        before=folder/'request.json'
        if before.exists()and regular(before,32768)!=encoded+b'\n':
            raise ValueError('Existing preparation request has changed')
        receipt_file=folder/'producer-receipt.json'
        if receipt_file.exists():
            receipt_bytes=regular(receipt_file,32768);receipt=json.loads(receipt_bytes)
            bundle=bind(output,10000000,ordered,PRODUCER_SHA.lower(),receipt)
            if receipt.get('source_outputs')!=dict(complete_sha256=bundle.receipt_sha256,
                                                  witness_sha256=list(bundle.witness_sha256)):
                raise ValueError('Preparation outputs differ from their owned completion receipt')
            _publish_ready(folder,configuration,bundle,receipt_bytes)
            return bundle,receipt
        if before.exists():
            raise PreparationRecoveryRequired('Unfinished preparation retained; reconcile its owned attempt before retry')
        atomic_bytes(before,encoded+b'\n')
        log=folder/'producer-output.log'
        with log.open('xb')as stream:
            owned=WindowsOwnedProcess(command,dict(os.environ)if environment is None else environment,
                # All input/output arguments are absolute. CreateProcessW's
                # working-directory limit is shorter than our durable job path.
                lease,stdout=stream,cwd=worker.anchor,cpu_budget=dict(logical_cpu=logical_cpu,memory_mib=memory_mib))
            identity=dict(pid=owned.pid,birth=owned.birth,image_sha256=PRODUCER_SHA,
                          configuration_sha256=configuration,wal_sequence=0)
            identity_file=folder/'producer-identity.json';atomic_bytes(identity_file,canonical(identity)+b'\n')
            owned.commit_identity(identity_file);owned.resume()
            while owned.poll()is None:
                if cancelled is not None and cancelled():raise PreparationCancelled('Preparation cancelled; source retained')
                if time.monotonic()-began>=timeout:raise TimeoutError('Preparation time budget reached; source retained')
                if log.stat().st_size>MAX_LOG_BYTES:raise BufferError('Preparation log budget exceeded')
                time.sleep(.025)
            code=owned.returncode;lease.record['returncode']=code
            owned.close(force=True);owned=None
        if not lease.released:raise RuntimeError('Preparation process ownership did not close')
        if log.stat().st_size>MAX_LOG_BYTES:raise BufferError('Preparation log budget exceeded')
        receipt=dict(args=command,exit=code,error=None,seconds=time.monotonic()-began,
                     all_owned_jobs_drained=True,GPU_used=False,logical_cpu=logical_cpu,memory_mib=memory_mib)
        if code!=0:
            atomic_bytes(folder/'producer-failure.json',canonical(dict(receipt,
                schema='PGN_CPU_PREPARATION_FAILURE_V1',configuration_sha256=configuration))+b'\n')
            raise RuntimeError('Preparation producer exited with code '+str(code)+'; no source result admitted')
        bundle=bind(output,10000000,ordered,PRODUCER_SHA.lower(),receipt)
        receipt['source_outputs']=dict(complete_sha256=bundle.receipt_sha256,witness_sha256=list(bundle.witness_sha256))
        receipt_bytes=canonical(receipt)+b'\n';atomic_bytes(receipt_file,receipt_bytes)
        _publish_ready(folder,configuration,bundle,receipt_bytes)
        return bundle,receipt
    except PreparationCancelled:
        # A graceful update/close is not a failed mathematical attempt. Publish
        # this receipt only after the exact child and its owned Job are drained.
        if owned is not None:
            owned.close(force=True);owned=None
        if lease.released:
            cancellation=dict(schema='PGN_CPU_PREPARATION_CANCELLED_V1',
                request_sha256=hashlib.sha256(regular(folder/'request.json',32768)).hexdigest(),
                owner_sha256=hashlib.sha256(regular(folder/'producer-owner.json',32768)).hexdigest())
            atomic_bytes(folder/'cancelled.json',canonical(cancellation)+b'\n')
        raise
    finally:
        try:
            if owned is not None:owned.close(force=True)
        finally:
            lock.release()
