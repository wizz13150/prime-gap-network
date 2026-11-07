"""Verified backup-copy migration between PGN testnet epochs only.

This preserves every wallet record and changes only SQLite's network marker.
It never opens a live Core wallet, changes a source file, imports a balance,
or establishes that the target Core accepts the wallet. That last check must
be performed against the expected target genesis by the owning application.
"""
from pathlib import Path
from contextlib import closing
import hashlib
import os
import re
import sqlite3
import tempfile

MAX_BYTES=256*1024*1024
MAX_RECORDS=1000000
MAX_BLOB=8*1024*1024


def _regular(path):
    if not path.is_absolute():raise ValueError('Explicit absolute wallet backup path required')
    for item in (path,*path.parents):
        if item.is_symlink()or(item.exists()and getattr(item.lstat(),'st_file_attributes',0)&0x400):
            raise ValueError('Wallet migration cannot follow a reparse point')
    if not path.is_file()or not 512<=path.stat().st_size<=MAX_BYTES:
        raise ValueError('Wallet backup size is outside the admitted bound')


def _sha(path):
    digest=hashlib.sha256()
    with path.open('rb')as stream:
        for part in iter(lambda:stream.read(1024*1024),b''):digest.update(part)
    return digest.hexdigest()


def _records(connection):
    connection.execute('PRAGMA trusted_schema=OFF')
    objects=connection.execute("SELECT type,name FROM sqlite_master WHERE name NOT LIKE 'sqlite_%'").fetchall()
    if objects!=[('table','main')]:raise ValueError('Unexpected wallet backup schema')
    columns=connection.execute('PRAGMA table_info(main)').fetchall()
    if [row[1:3]for row in columns]!=[('key','BLOB'),('value','BLOB')]:
        raise ValueError('Expected the native wallet key/value table')
    count,key_bytes,value_bytes=connection.execute('SELECT count(*),coalesce(max(length(key)),0),coalesce(max(length(value)),0) FROM main').fetchone()
    if not 0<=count<=MAX_RECORDS or not 0<=max(key_bytes,value_bytes)<=MAX_BLOB:
        raise ValueError('Wallet records exceed the admitted bound')
    digest=hashlib.sha256(b'PGN-WALLET-RECORDS-V1\0');total=0
    for key,value in connection.execute('SELECT key,value FROM main ORDER BY key'):
        if type(key)is not bytes or type(value)is not bytes:raise ValueError('Wallet records must be blobs')
        if key.startswith(b'\x02tx'):
            raise ValueError('A wallet with transactions needs explicit descriptor migration, not an epoch marker copy')
        total+=len(key)+len(value)
        if total>MAX_BYTES:raise ValueError('Wallet record volume exceeds its bound')
        for part in (key,value):digest.update(len(part).to_bytes(8,'little'));digest.update(part)
    return count,digest.hexdigest()


def migrate_backup(source,target,*,expected_sha256,source_magic,target_magic,source_chain,target_chain):
    if source_chain!='test'or target_chain!='test':
        raise ValueError('Epoch migration is restricted to testnet; mainnet wallets remain separate')
    if (type(source_magic)is not str or type(target_magic)is not str
            or not re.fullmatch('d7544e[0-9a-f]{2}',source_magic)
            or not re.fullmatch('d7544e[0-9a-f]{2}',target_magic)or source_magic==target_magic):
        raise ValueError('Two distinct explicit PGN testnet epoch markers required')
    if type(expected_sha256)is not str or not re.fullmatch('[0-9a-f]{64}',expected_sha256):
        raise ValueError('Verified backup digest required')
    source=Path(source);target=Path(target);_regular(source)
    if not target.is_absolute()or target.exists()or not target.parent.is_dir():
        raise ValueError('A new absolute destination in an existing folder is required')
    for item in (target.parent,*target.parent.parents):
        if item.is_symlink()or getattr(item.lstat(),'st_file_attributes',0)&0x400:
            raise ValueError('Wallet destination cannot follow a reparse point')
    if any(Path(str(source)+suffix).exists()for suffix in ('-wal','-shm','-journal')):
        raise ValueError('Use a completed Core backup, not a live wallet database')
    if _sha(source)!=expected_sha256:raise ValueError('Wallet backup changed before migration')
    with source.open('rb')as stream:
        header=stream.read(72)
    if header[:16]!=b'SQLite format 3\0'or header[68:72].hex()!=source_magic:
        raise ValueError('Wallet backup belongs to a different format or network')
    descriptor,name=tempfile.mkstemp(prefix='.pgn-wallet-epoch-',suffix='.sqlite',dir=target.parent)
    os.close(descriptor);temporary=Path(name);before=None;after=None
    try:
        with closing(sqlite3.connect(source.as_uri()+'?mode=ro',uri=True))as original:
            before=_records(original)
            with closing(sqlite3.connect(temporary))as migrated:
                original.backup(migrated)
                marker=int(target_magic,16);marker=marker if marker<2**31 else marker-2**32
                migrated.execute('PRAGMA application_id='+str(marker));migrated.commit()
                after=_records(migrated)
                if migrated.execute('PRAGMA integrity_check').fetchone()!=('ok',):
                    raise ValueError('Migrated backup failed SQLite integrity')
        if before!=after or _sha(source)!=expected_sha256:
            raise ValueError('Wallet records or original backup changed during migration')
        with temporary.open('r+b')as stream:
            stream.seek(68)
            if stream.read(4).hex()!=target_magic:raise ValueError('Target network marker was not committed')
            stream.flush();os.fsync(stream.fileno())
        output_sha=_sha(temporary)
        # Exclusive publication: an existing wallet is never overwritten.
        os.link(temporary,target)
        return dict(schema='PGN_TESTNET_WALLET_EPOCH_COPY_V1',source_sha256=expected_sha256,
                    target_sha256=output_sha,source_magic=source_magic,target_magic=target_magic,
                    records=before[0],records_sha256=before[1],records_unchanged=True,
                    source_unchanged=True,Core_acceptance_tested=False)
    finally:
        temporary.unlink(missing_ok=True)
