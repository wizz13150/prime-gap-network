"""Preserve client-local bootstrap configuration across code-only host updates."""
from pathlib import Path
import json
import os
import re
import uuid

def preserve_bootstrap(source,target,genesis):
    source,target=Path(source),Path(target)
    if source.is_symlink()or target.is_symlink():raise ValueError('Redirected runtime folder')
    source,target=source.resolve(strict=True),target.resolve(strict=True)
    if source==target or source.parent!=target.parent:raise ValueError('Runtime switch folders must be distinct siblings')
    file=source/'bootstrap.json';destination=target/'bootstrap.json'
    if not file.exists():return dict(preserved=False,reason='No existing local bootstrap')
    if file.is_symlink()or not file.is_file()or file.stat().st_size>8192:raise ValueError('Invalid local bootstrap file')
    raw=file.read_bytes()
    def unique(pairs):
        value={}
        for key,item in pairs:
            if key in value:raise ValueError('Duplicate bootstrap field')
            value[key]=item
        return value
    value=json.loads(raw,object_pairs_hook=unique);tls=value.get('tls',{});checkpoint=value.get('checkpoint',{})
    if (value.get('schema')!='PGN_TESTNET_BOOTSTRAP_V1'or value.get('genesis')!=genesis
        or not isinstance(tls.get('host'),str)or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]{0,252}',tls['host'])
        or type(tls.get('port'))is not int or not 1<=tls['port']<=65535
        or type(checkpoint.get('height'))is not int or checkpoint['height']<1
        or not isinstance(checkpoint.get('hash'),str)or not re.fullmatch(r'[0-9a-f]{64}',checkpoint['hash'])):
        raise ValueError('Local bootstrap network or structure differs')
    if destination.exists():
        if destination.is_symlink()or not destination.is_file()or destination.stat().st_size>8192 or destination.read_bytes()!=raw:
            raise ValueError('Staged bootstrap differs; existing configuration retained')
        return dict(preserved=True,already_present=True,bytes=len(raw))
    temporary=target/('bootstrap.'+uuid.uuid4().hex+'.pending')
    try:
        with temporary.open('xb')as stream:stream.write(raw);stream.flush();os.fsync(stream.fileno())
        try:os.link(temporary,destination)
        except FileExistsError:
            if destination.is_symlink()or destination.read_bytes()!=raw:raise ValueError('Concurrent bootstrap configuration differs')
    finally:
        if temporary.exists():temporary.unlink()
    return dict(preserved=True,already_present=False,bytes=len(raw))
