"""Prepare signed update metadata locally; never uploads or executes a payload.

Publisher keys stay outside the public output, encrypted with Windows DPAPI for
the current account. Four distinct role keys use the reference TUF serializer.
This is a single-maintainer testnet setup, not a threshold/offline-key service.
"""
from datetime import datetime,timedelta,timezone
from pathlib import Path
import ctypes
import hashlib
import io
import json
import os
import re
import shutil
import uuid
import zipfile
from types import SimpleNamespace
from cryptography.hazmat.primitives.serialization import load_pem_private_key
from securesystemslib.signer import CryptoSigner
from tuf.api.metadata import Metadata,Root,Targets,Snapshot,Timestamp,MetaFile,TargetFile
from tuf.api.serialization.json import JSONSerializer
import stage_component_bundle as stage

ROLES=('root','targets','snapshot','timestamp')


def protected(data,entropy,decrypt=False):
    stage.require(os.name=='nt'and type(data)is bytes and 0<len(data)<=65536,'Bounded Windows DPAPI input required')
    from ctypes import wintypes
    class Blob(ctypes.Structure):
        _fields_=[('length',wintypes.DWORD),('data',ctypes.POINTER(ctypes.c_ubyte))]
    def blob(value):
        buffer=(ctypes.c_ubyte*len(value)).from_buffer_copy(value)
        return Blob(len(value),buffer),buffer
    incoming,hold=blob(data);purpose,hold_purpose=blob(entropy);out=Blob()
    crypt=ctypes.WinDLL('crypt32',use_last_error=True);kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.LocalFree.argtypes=[ctypes.c_void_p];kernel.LocalFree.restype=ctypes.c_void_p
    if decrypt:
        fn=crypt.CryptUnprotectData;fn.argtypes=[ctypes.POINTER(Blob),ctypes.c_void_p,ctypes.POINTER(Blob),ctypes.c_void_p,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(Blob)]
        args=(ctypes.byref(incoming),None,ctypes.byref(purpose),None,None,1,ctypes.byref(out))
    else:
        fn=crypt.CryptProtectData;fn.argtypes=[ctypes.POINTER(Blob),wintypes.LPCWSTR,ctypes.POINTER(Blob),ctypes.c_void_p,ctypes.c_void_p,wintypes.DWORD,ctypes.POINTER(Blob)]
        args=(ctypes.byref(incoming),'PGN update publisher',ctypes.byref(purpose),None,None,1,ctypes.byref(out))
    fn.restype=wintypes.BOOL
    if not fn(*args):raise ctypes.WinError(ctypes.get_last_error())
    try:return ctypes.string_at(out.data,out.length)
    finally:kernel.LocalFree(out.data)


def initialize_keys(directory,network,genesis,now=None):
    directory=stage._plain_chain(directory)
    stage.require(network in ('testnet','main')and stage.DIGEST.fullmatch(genesis),'Explicit chain identity required')
    stage.require(not directory.exists(),'Publisher key store already exists')
    pending=directory.parent/('.pending-'+uuid.uuid4().hex);pending.mkdir(mode=0o700)
    try:
        now=now or datetime.now(timezone.utc)
        root=Metadata(Root(version=1,expires=now+timedelta(days=365),consistent_snapshot=True));ids={}
        signers={role:CryptoSigner.generate_ed25519()for role in ROLES}
        for role,signer in signers.items():
            entropy=('PGN-TUF-V1:'+network+':'+genesis+':'+role).encode()
            with (pending/(role+'.dpapi')).open('xb')as stream:
                stream.write(protected(signer.private_bytes,entropy));stream.flush();os.fsync(stream.fileno())
            root.signed.add_key(signer.public_key,role);ids[role]=signer.public_key.keyid
        root.sign(signers['root']);raw=root.to_bytes(JSONSerializer(compact=True))
        values={'root.json':raw,'key-store.json':(json.dumps(dict(schema='PGN_PUBLISHER_KEY_STORE_V1',network=network,genesis_hash=genesis,roles=ids,root_sha256=stage.sha(raw)),sort_keys=True)+'\n').encode()}
        for name,contents in values.items():
            with (pending/name).open('xb')as stream:stream.write(contents);stream.flush();os.fsync(stream.fileno())
        stage._publish_pending(pending,directory,lambda:False)
        return raw
    finally:
        if pending.exists():stage._remove_pending(pending,directory.parent)


def _signer(directory,role):
    store=stage._json(stage._read_plain(directory/'key-store.json',8192))
    stage.require(store.get('schema')=='PGN_PUBLISHER_KEY_STORE_V1'and role in ROLES,'Invalid publisher key store')
    entropy=('PGN-TUF-V1:'+store['network']+':'+store['genesis_hash']+':'+role).encode()
    encrypted=stage._read_plain(directory/(role+'.dpapi'),65536)
    signer=CryptoSigner(load_pem_private_key(protected(encrypted,entropy,True),password=None))
    stage.require(signer.public_key.keyid==store['roles'][role],'Publisher key identity differs')
    return signer,store


def publish(directory,bundle,build_receipt,output,version,now=None,*,previous_targets=None):
    # Serialize reservations, including failed signing attempts. A later build
    # must not reuse the same signed metadata version for different contents.
    with stage.owned_lock(stage._plain_chain(directory)):
        return _publish(directory,bundle,build_receipt,output,version,now,previous_targets=previous_targets)


def publish_in_memory(directory,archive,build_receipt,output,version,now=None,*,previous_targets=None):
    """Verify bounded archive bytes and prepare metadata without a disk copy.

    The caller must upload the exact archive as a separate immutable target
    before making this metadata public. No payload or private key is uploaded.
    """
    stage.require(type(archive)is bytes and 0<len(archive)<=stage.MAX_ARCHIVE,
                  'Bounded immutable archive bytes required')
    with stage.owned_lock(stage._plain_chain(directory)):
        return _publish(directory,None,build_receipt,output,version,now,memory=archive,previous_targets=previous_targets)


class _MemoryArchive:
    def __init__(self,data):self.data=data
    def stat(self):return SimpleNamespace(st_size=len(self.data))
    def open(self,mode):
        stage.require(mode=='rb','Read-only archive required')
        return io.BytesIO(self.data)


def _publication_state(path,value):
    temporary=path.with_name('.publication-'+uuid.uuid4().hex+'.pending')
    try:
        with temporary.open('xb')as stream:
            stream.write((json.dumps(value,sort_keys=True)+'\n').encode());stream.flush();os.fsync(stream.fileno())
        os.replace(temporary,path)
    finally:temporary.unlink(missing_ok=True)


def retained_platform_targets(root,raw,previous_version,platform):
    """Preserve the other OS's authenticated catalogue and immutable bundle."""
    previous=Metadata.from_bytes(raw)
    stage.require(isinstance(previous.signed,Targets)and previous.signed.version==previous_version,'Previous targets are not the current publisher version')
    Metadata.from_bytes(root).signed.verify_delegate('targets',previous.signed_bytes,previous.signatures)
    opposite='components.json'if platform=='linux-x86_64'else'components-linux-x86_64.json'
    pattern=r'components-\d+\.zip'if platform=='linux-x86_64'else r'components-linux-x86_64-\d+\.zip'
    names=previous.signed.targets
    stage.require(len(names)<=4,'Previous platform target set exceeds bound')
    retained={name:value for name,value in names.items()if name==opposite or re.fullmatch(pattern,name)}
    stage.require(not retained or(opposite in retained and len(retained)==2),'Previous platform catalogue/bundle pair is incomplete')
    return retained


def _publish(directory,bundle,build_receipt,output,version,now=None,*,memory=None,previous_targets=None):
    directory=stage._plain_chain(directory);bundle=stage._plain_chain(bundle)if memory is None else _MemoryArchive(memory);output=stage._plain_chain(output)
    stage.require(not output.exists()and output.parent.is_dir(),'New publication output required')
    stage.require(not output.is_relative_to(directory)and not directory.is_relative_to(output),'Public output overlaps private key store')
    stage.require(type(version)is int and 1<=version<=2**31-1,'Bounded positive metadata version required')
    _,store=_signer(directory,'root');root=stage._read_plain(directory/'root.json',262144)
    stage.require(stage.sha(root)==store['root_sha256'],'Publisher root differs')
    manifest=build_receipt['manifest']
    stage.require(manifest['network']==store['network']and manifest['genesis_hash']==store['genesis_hash'],'Publisher chain differs from bundle')
    stage.require(bundle.stat().st_size==build_receipt['bytes'],'Publication bundle size differs')
    with bundle.open('rb')as stream:actual=hashlib.file_digest(stream,'sha256').hexdigest()
    stage.require(actual==build_receipt['archive_sha256'],'Publication bundle fingerprint differs')
    with bundle.open('rb')as stream:stage._zip_preflight(stream)
    with bundle.open('rb')as archive_stream,zipfile.ZipFile(archive_stream)as archive:
        entry=archive.getinfo('component-bundle.json');stage.require(entry.file_size<=stage.MAX_MANIFEST,'Bundle manifest exceeds bound')
        stage.require(stage._json(archive.read(entry))==manifest,'Publication receipt describes another archive')
    platform=manifest['platform'];stage.require(platform in ('windows-x86_64','linux-x86_64'),'Unsupported publication platform')
    catalogue='components-linux-x86_64.json'if platform=='linux-x86_64'else'components.json'
    counter=directory/'publication-state.json';prior=None;retained={}
    if counter.exists():
        prior=stage._json(stage._read_plain(counter,8192))
        stage.require(version>prior['metadata_version'],'Signed metadata version was already reserved')
        stage.require(manifest['sequence']>=prior['bundle_sequence'],'Bundle sequence would roll back')
        stage.require(manifest['sequence']!=prior['bundle_sequence']or actual==prior['bundle_sha256'],'An immutable bundle sequence cannot describe different bytes')
    if previous_targets is not None:
        stage.require(prior is not None,'Previous publication state is required')
        retained=retained_platform_targets(root,stage._read_plain(Path(previous_targets),stage.MAX_MANIFEST),prior['metadata_version'],platform)
    elif prior is not None:
        stage.require(prior.get('catalogues',['components.json'])==[catalogue],'Current signed targets are required to preserve the other platform')
    reservation=dict(schema='PGN_PUBLISHER_SEQUENCE_V1',state='reserved',metadata_version=version,bundle_sequence=manifest['sequence'],bundle_sha256=actual,catalogues=sorted([catalogue]+[name for name in retained if name.endswith('.json')]))
    _publication_state(counter,reservation)
    bundle_name=('components-linux-x86_64-'if platform=='linux-x86_64'else'components-')+str(manifest['sequence'])+'.zip'
    catalog=json.dumps(dict(schema='PGN_AUTHENTICATED_UPDATE_CATALOG_V1',bundle_target=bundle_name,manifest=manifest),separators=(',',':')).encode()
    stage.require(len(catalog)<=stage.MAX_MANIFEST,'Update catalog exceeds bound')
    now=now or datetime.now(timezone.utc)
    targets=Metadata(Targets(version=version,expires=now+timedelta(days=90)))
    targets.signed.targets.update(retained)
    targets.signed.targets[catalogue]=TargetFile.from_data(catalogue,catalog,['sha256'])
    targets.signed.targets[bundle_name]=TargetFile(length=bundle.stat().st_size,hashes={'sha256':actual},path=bundle_name)
    targets.sign(_signer(directory,'targets')[0]);traw=targets.to_bytes(JSONSerializer(compact=True))
    snapshot=Metadata(Snapshot(version=version,expires=now+timedelta(days=30)));snapshot.signed.meta['targets.json']=MetaFile.from_data(version,traw,['sha256']);snapshot.sign(_signer(directory,'snapshot')[0]);sraw=snapshot.to_bytes(JSONSerializer(compact=True))
    timestamp=Metadata(Timestamp(version=version,expires=now+timedelta(days=14)));timestamp.signed.snapshot_meta=MetaFile.from_data(version,sraw,['sha256']);timestamp.sign(_signer(directory,'timestamp')[0]);tsraw=timestamp.to_bytes(JSONSerializer(compact=True))
    pending=output.parent/('.pending-'+uuid.uuid4().hex);pending.mkdir()
    try:
        metadata=pending/'metadata';assets=pending/'targets';metadata.mkdir();assets.mkdir()
        for name,raw in [('1.root.json',root),('root.json',root),('targets.json',traw),(str(version)+'.targets.json',traw),('snapshot.json',sraw),(str(version)+'.snapshot.json',sraw),('timestamp.json',tsraw)]:
            (metadata/name).write_bytes(raw)
        (assets/(stage.sha(catalog)+'.'+catalogue)).write_bytes(catalog)
        if memory is None:shutil.copyfile(bundle,assets/(actual+'.'+bundle_name))
        stage._publish_pending(pending,output,lambda:False)
    finally:
        if pending.exists():stage._remove_pending(pending,output.parent)
    _publication_state(counter,dict(reservation,state='prepared'))
    return dict(schema='PGN_SIGNED_PUBLICATION_RECEIPT_V1',root_sha256=store['root_sha256'],metadata_version=version,
        bundle_sha256=actual,files=9 if memory is None else 8,archive_included=memory is None,
        private_key_files_exported=False,uploaded=False,catalogue=catalogue,retained_targets=sorted(retained))
