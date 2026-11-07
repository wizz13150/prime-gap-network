"""Explicit pinned scientific source bundles; never a replacement fallback."""
from dataclasses import dataclass
from pathlib import Path
import ctypes
import json
import os
import sys

from horizon_scientific_identity import TABLE_SHA, PRODUCER_SHA
from pgn_native_contract import file_sha

DESCRIPTOR='network/scientific-source.json'


class UnsupportedScientificArchitecture(ValueError):
    pass


def qualified_inputs(worker,table,architecture):
    # Frozen builds contain these modules. Source checkouts resolve only this
    # known sibling directory, and only for an explicitly selected source mode.
    shared=Path(__file__).resolve().parents[1]/'shared_composite'
    if shared.is_dir()and str(shared)not in sys.path:
        sys.path.insert(0,str(shared))
    from horizon_complement_runtime import qualified_inputs as inspect
    return inspect(worker,table,architecture)


@dataclass(frozen=True)
class ScientificBundle:
    worker: Path
    table: Path
    producer: Path
    architecture: int
    memory_mib: int
    timeout: int


def _path(root,relative,expected):
    if relative!=expected:
        raise ValueError('Scientific component must use its fixed bundle-relative path')
    path=root/relative
    if not path.resolve(strict=True).is_relative_to(root):
        raise ValueError('Scientific component escaped its bundle')
    for value in [path,*path.parents]:
        if value==root.parent:break
        if value.is_symlink()or getattr(value.lstat(),'st_file_attributes',0)&0x400:
            raise ValueError('Scientific component cannot be a reparse point')
    return path


def load(bundle,architecture,*,required=False):
    root=Path(bundle).resolve(strict=True);file=root/DESCRIPTOR
    if not file.exists():
        if required:raise ValueError('This worker requires a complete scientific source bundle')
        return None
    from horizon_preparation_process import regular
    file=_path(root,DESCRIPTOR,DESCRIPTOR)
    descriptor=json.loads(regular(file,8192))
    if type(descriptor)is not dict:
        raise ValueError('Unqualified scientific source descriptor')
    common={'schema','table','producer','domain_end_exclusive','preparation'}
    if descriptor.get('schema')=='PGN_SCOPED_SCIENTIFIC_SOURCE_V1':
        if (set(descriptor)!=common|{'architecture','worker'}
                or type(descriptor['architecture'])is not int or descriptor['architecture']!=86):
            raise ValueError('Unqualified scientific source descriptor')
        workers={'86':descriptor['worker']}
    elif descriptor.get('schema')=='PGN_SCOPED_SCIENTIFIC_SOURCE_V2':
        workers=descriptor.get('workers')
        if (set(descriptor)!=common|{'workers'}or type(workers)is not dict
                or not workers or not set(workers)<={'86','120'}):
            raise ValueError('Exact bounded scientific architecture map required')
    else:
        raise ValueError('Unqualified scientific source descriptor')
    if descriptor['domain_end_exclusive']!='1000000000000000000000':
        raise ValueError('Unqualified scientific source domain')
    for arch,row in workers.items():
        if (type(row)is not dict or set(row)!={'path','sha256'}
                or row['path']!=f'bin/prime-gap-worker-sm{arch}-k64.exe'):
            raise ValueError('Exact architecture-specific worker path required')
    if type(architecture)is not int or str(architecture)not in workers:
        raise UnsupportedScientificArchitecture('No qualified scientific source for this GPU architecture')
    descriptor=dict(descriptor,worker=workers[str(architecture)])
    for key in ('worker','table','producer'):
        if type(descriptor[key])is not dict or set(descriptor[key])!={'path','sha256'}:
            raise ValueError('Exact scientific component path and digest required')
    worker=_path(root,descriptor['worker']['path'],f'bin/prime-gap-worker-sm{architecture}-k64.exe')
    table=_path(root,descriptor['table']['path'],'bin/canonical-orders.bin')
    producer=_path(root,descriptor['producer']['path'],'bin/pgn-psp-tail-producer.exe')
    worker,table,metadata=qualified_inputs(worker,table,architecture)
    if (descriptor['worker']['sha256']!=metadata['image_sha256']
            or descriptor['table']['sha256']!=TABLE_SHA
            or descriptor['producer']['sha256']!=PRODUCER_SHA or file_sha(producer)!=PRODUCER_SHA):
        raise ValueError('Scientific bundle component differs from its qualified image')
    resources=descriptor['preparation']
    if (type(resources)is not dict or set(resources)!={'threads','priority','memory_mib','timeout_seconds'}
            or type(resources['threads'])is not int or resources['threads']!=1 or resources['priority']!='idle'
            or type(resources['memory_mib'])is not int or not 128<=resources['memory_mib']<=1024
            or type(resources['timeout_seconds'])is not int or not 60<=resources['timeout_seconds']<=3600):
        raise ValueError('Bounded single-processor source preparation resources required')
    return ScientificBundle(worker,table,producer,architecture,resources['memory_mib'],resources['timeout_seconds'])


def qualified_bundle_workers(bundle,architecture):
    specification=load(bundle,architecture)
    if specification is not None:return [(64,specification.worker)]
    from horizon_engine_capabilities import qualified_workers
    return qualified_workers(bundle,architecture)


def _processor():
    if os.name!='nt':raise ValueError('The qualified scientific producer currently requires Windows')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.GetCurrentProcess.restype=ctypes.c_void_p
    kernel.GetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.POINTER(ctypes.c_size_t),ctypes.POINTER(ctypes.c_size_t)]
    process=ctypes.c_size_t();system=ctypes.c_size_t()
    if not kernel.GetProcessAffinityMask(kernel.GetCurrentProcess(),ctypes.byref(process),ctypes.byref(system)):
        raise ctypes.WinError(ctypes.get_last_error())
    available=[i for i in range(64)if process.value&(1<<i)]
    if not available:raise ValueError('No admitted preparation processor')
    return 4 if 4 in available else available[0]


def create_engine(bundle,state_root,gpu_uuid,architecture,observer,*,required=False,stopping=None):
    from horizon_engine_host import EngineService
    specification=load(bundle,architecture,required=required)
    if specification is None:
        return EngineService(bundle,state_root,gpu_uuid,architecture,observer=observer)
    from pgn_prime_proof import PrimeProof
    from horizon_preparation_provider import PreparationProvider
    from horizon_preparation_resolver import PreparationResolver
    from horizon_complement_runtime import DeferredComplementRuntime
    proof=PrimeProof(bundle)
    root=Path(state_root)/'scientific-preparation'
    provider=PreparationProvider(root/'provider',specification.producer,proof.prime)
    resolver=PreparationResolver(root/'resolver',provider,logical_cpu=_processor(),
        memory_mib=specification.memory_mib,timeout=specification.timeout,stopping=stopping,prefetch=True)
    try:
        runtime=DeferredComplementRuntime(specification.worker,specification.table,architecture,resolver)
        return EngineService(bundle,state_root,gpu_uuid,architecture,observer=observer,complement=runtime)
    except BaseException:
        resolver.close()
        raise
