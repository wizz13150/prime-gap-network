"""Bounded conservation of exact local observations before index retirement.

This file does not certify primality, network acceptance, or complete coverage
below the configured collection cutoff. The caller owns a completed, quiescent
ticket. Only its bound SQLite index is read; no wallet or chain is accessed.
"""
from pathlib import Path
from contextlib import closing
import gzip
import hashlib
import json
import os
import sqlite3
import uuid
from pgn_paths import readonly_sqlite_uri

NAME = 'scientific-gaps.jsonl.gz'
SCHEMA = 'PGN_LOCAL_SCIENTIFIC_GAPS_V1'
MAX_FILE = 64 * 1024 * 1024
MAX_RAW = 256 * 1024 * 1024
MAX_RECORDS = 2_000_000
MAX_HEADER = 16384
MAX_ROW = 512
COLUMNS = ['lower_prime', 'upper_prime', 'gap', 'sequence', 'centile',
           'visible', 'count_ok', 'detail_ok']


def canonical(value):
    return json.dumps(value,sort_keys=True,separators=(',',':'),allow_nan=False).encode()


def digest(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream,'sha256').hexdigest()


def plain(path,maximum):
    if (path.is_symlink() or not path.is_file()
            or getattr(path.lstat(),'st_file_attributes',0)&0x400
            or path.stat().st_size>maximum):
        raise ValueError('Scientific gap file is linked, unavailable, or oversized')


def header(configuration):
    return dict(schema=SCHEMA,ticket_id=configuration['ticket_id'],
        configuration_sha256=hashlib.sha256(canonical(configuration)).hexdigest(),
        start=configuration['start'],end=configuration['end'],
        collection_policy=configuration['threshold_policy'],columns=COLUMNS,
        stage='local_observations_before_scientific_finalization',
        network_acceptance_proven=False)


def checked_row(row):
    if type(row)is not list or len(row)!=8:
        raise ValueError('Invalid scientific gap row')
    lo,hi,gap,sequence,centile,*flags=row
    for value in (lo,hi):
        if (type(value)is not str or not value.isascii() or not value.isdecimal()
                or len(value)>39 or str(int(value))!=value or not 2<=int(value)<2**128):
            raise ValueError('Noncanonical exact prime-position observation')
    if (type(gap)is not int or not 0<gap<2**63 or int(hi)-int(lo)!=gap
            or type(sequence)is not int or not 0<=sequence<2**63
            or type(centile)is not int or not 0<=centile<4096
            or any(x is not None and(type(x)is not int or x not in(0,1))for x in flags)):
        raise ValueError('Scientific gap arithmetic or source flags differ')
    return row


def _unique(pairs):
    out={}
    for key,value in pairs:
        if key in out:raise ValueError('Duplicate scientific gap metadata key')
        out[key]=value
    return out


def check_descriptor(folder,descriptor,configuration):
    """Cheap availability check for presentation; does not verify file content."""
    expected=header(configuration);path=Path(folder)/NAME
    if (type(descriptor)is not dict or set(descriptor)!=
            {'schema','path','sha256','bytes','records','configuration_sha256'}
            or descriptor['schema']!=SCHEMA or descriptor['path']!=NAME
            or descriptor['configuration_sha256']!=expected['configuration_sha256']
            or type(descriptor['bytes'])is not int or not 0<descriptor['bytes']<=MAX_FILE
            or type(descriptor['records'])is not int or not 0<=descriptor['records']<=MAX_RECORDS):
        raise ValueError('Scientific gap descriptor differs')
    plain(path,MAX_FILE)
    if path.stat().st_size!=descriptor['bytes']:
        raise ValueError('Scientific gap archive size differs')
    return path


def verify(folder,descriptor,configuration):
    """Fully stream-verify a sidecar before treating work files as redundant."""
    expected=header(configuration);path=check_descriptor(folder,descriptor,configuration)
    if digest(path)!=descriptor['sha256']:
        raise ValueError('Scientific gap archive fingerprint differs')
    count=0;total=0;previous=-1
    try:
        with gzip.open(path,'rb')as stream:
            first=stream.readline(MAX_HEADER+1);total+=len(first)
            if len(first)>MAX_HEADER or json.loads(first,object_pairs_hook=_unique)!=expected:
                raise ValueError('Scientific gap header differs')
            while True:
                raw=stream.readline(MAX_ROW+1);total+=len(raw)
                if not raw or len(raw)>MAX_ROW or total>MAX_RAW:
                    raise ValueError('Scientific gap record budget or footer differs')
                value=json.loads(raw,object_pairs_hook=_unique)
                if type(value)is dict:
                    if value!={'records':count} or stream.read(1):
                        raise ValueError('Scientific gap footer differs')
                    break
                row=checked_row(value);lower=int(row[0]);count+=1
                if lower<=previous or count>MAX_RECORDS:
                    raise ValueError('Duplicate, unordered, or excessive gap observations')
                previous=lower
    except (OSError,EOFError,json.JSONDecodeError)as error:
        raise ValueError('Scientific gap archive cannot be decoded')from error
    if count!=descriptor['records']:raise ValueError('Scientific gap record count differs')
    return descriptor


def create(folder,configuration):
    """Publish only after complete streaming validation; originals stay intact."""
    folder=Path(folder);index=folder/'session-index.sqlite3';plain(index,128*1024*1024)
    if folder.name!=configuration['ticket_id']:raise ValueError('Scientific gap ticket differs')
    for suffix in ('-wal','-shm'):
        side=index.with_name(index.name+suffix)
        if side.exists():plain(side,32*1024*1024)
    target=folder/NAME
    temporary=folder/('.scientific-gaps-'+uuid.uuid4().hex+'.pending')
    expected=header(configuration);first=canonical(expected)+b'\n'
    if len(first)>MAX_HEADER:raise ValueError('Scientific gap collection policy exceeds its budget')
    count=0;raw_bytes=len(first)
    try:
        with closing(sqlite3.connect(readonly_sqlite_uri(index),uri=True))as connection:
            connection.execute('PRAGMA query_only=ON');connection.execute('BEGIN')
            value=connection.execute("SELECT v FROM meta WHERE k='binding'").fetchone()
            if value is None or json.loads(value[0]).get('configuration_hash')!=expected['configuration_sha256']:
                raise ValueError('Scientific index configuration binding differs')
            fields={r[1]for r in connection.execute('PRAGMA table_info(gaps)')}
            if not {'lo','hi','gap','seq','bin'}<=fields:raise ValueError('Scientific gap index schema differs')
            if connection.execute('SELECT COUNT(*) FROM gaps').fetchone()[0]>MAX_RECORDS:
                raise ValueError('Scientific gap count exceeds its archive budget')
            flags=','.join(name if name in fields else 'NULL'for name in ('visible','count_ok','detail_ok'))
            cursor=connection.execute('SELECT lo,hi,gap,seq,bin,'+flags+' FROM gaps ORDER BY lo')
            with temporary.open('xb')as output:
                with gzip.GzipFile(filename='',mode='wb',fileobj=output,mtime=0)as compressed:
                    compressed.write(first)
                    for low,high,gap,sequence,centile,*source_flags in cursor:
                        if type(low)is not bytes or type(high)is not bytes or len(low)!=16 or len(high)!=16:
                            raise ValueError('Scientific index positions are not exact 128-bit values')
                        row=checked_row([str(int.from_bytes(low,'big')),str(int.from_bytes(high,'big')),
                            gap,sequence,centile,*source_flags])
                        raw=canonical(row)+b'\n';raw_bytes+=len(raw);count+=1
                        if len(raw)>MAX_ROW or raw_bytes>MAX_RAW or output.tell()>MAX_FILE:
                            raise ValueError('Scientific gap archive exceeds its storage budget')
                        compressed.write(raw)
                    compressed.write(canonical(dict(records=count))+b'\n')
                output.flush();os.fsync(output.fileno())
            if temporary.stat().st_size>MAX_FILE:raise ValueError('Compressed scientific gap archive exceeds its budget')
        descriptor=dict(schema=SCHEMA,path=NAME,sha256=digest(temporary),bytes=temporary.stat().st_size,
            records=count,configuration_sha256=expected['configuration_sha256'])
        try:os.link(temporary,target)
        except FileExistsError:
            plain(target,MAX_FILE)
            if target.stat().st_size!=descriptor['bytes'] or digest(target)!=descriptor['sha256']:
                raise ValueError('Existing scientific gap archive differs; originals retained')
        return verify(folder,descriptor,configuration)
    finally:
        temporary.unlink(missing_ok=True)
