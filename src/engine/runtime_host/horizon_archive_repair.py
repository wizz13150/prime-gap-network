"""Read-only reconciliation of a finalized report and its complete raw index."""
from pathlib import Path
import copy
import hashlib
import json
import sqlite3
from pgn_paths import readonly_sqlite_uri
from pgn_gap_projection import MAX_PAIRS,checked_pairs,project,presentation
from report_commitment import canonical_bytes,validate_computed_report


def marker(folder,configuration,original,corrected):
    from horizon_preparation_process import regular
    raw=regular(folder/'source-finalization.json',8*2**20)
    checksum=hashlib.sha256(raw).hexdigest()
    if regular(folder/'source-finalization.sha256',65)!=checksum.encode()+b'\n':
        raise ValueError('Scientific archive correction checksum changed')
    value=json.loads(raw)
    if (value.get('schema')!='PGN_SCOPED_SOURCE_FINALIZATION_V1'
            or value.get('ticket_id')!=folder.name
            or value.get('configuration_sha256')!=hashlib.sha256(canonical_bytes(configuration)).hexdigest()
            or value.get('original_report_sha256')!=original['sha256']
            or value.get('corrected_report')!=corrected):
        raise ValueError('Scientific archive correction belongs to another computation')
    return value,checksum


def primary_pairs(folder,configuration):
    from pgn_index import MAX_SESSION_INDEX_BYTES
    path=folder/'session-index.sqlite3'
    if (not path.is_file()or path.is_symlink()or path.stat().st_size>MAX_SESSION_INDEX_BYTES
            or getattr(path.lstat(),'st_file_attributes',0)&0x400):
        raise ValueError('Complete original archive index is unavailable')
    connection=sqlite3.connect(readonly_sqlite_uri(path),uri=True,timeout=.1,isolation_level=None)
    try:
        connection.execute('PRAGMA query_only=ON');connection.execute('BEGIN')
        meta={}
        for key,value in connection.execute("SELECT k,v FROM meta WHERE k IN ('binding','progress','projection')"):
            if len(value)>1048576:raise ValueError('Archive source metadata exceeds its budget')
            meta[key]=json.loads(value)
        length=int(configuration['end'])-int(configuration['start'])
        if (meta['binding']['configuration_hash']!=hashlib.sha256(canonical_bytes(configuration)).hexdigest()
                or int(meta['progress']['positions'])!=length
                or int(meta['projection']['prefix'])!=int(configuration['end'])):
            raise ValueError('Archive source index is not the complete original computation')
        rows=connection.execute('SELECT lo,hi FROM gaps ORDER BY lo LIMIT ?', (MAX_PAIRS+1,)).fetchall()
        return checked_pairs([(int.from_bytes(lo,'big'),int.from_bytes(hi,'big'))for lo,hi in rows],configuration)
    finally:connection.close()


def reconcile(folder,configuration,original,corrected,frame):
    value,checksum=marker(folder,configuration,original,corrected)
    work=dict(ticket=folder.name,start=configuration['start'],length=str(int(configuration['end'])-int(configuration['start'])))
    validate_computed_report(original,work);validate_computed_report(corrected,work)
    pairs=primary_pairs(folder,configuration)
    def matches(report,pairs):
        counts,best=project(pairs,configuration)
        if counts!=report['body']['counts']or best!=report['body']['best']:
            raise ValueError('Complete archive projection differs from its immutable report')
    matches(original,pairs)
    def delta(name):
        values=value.get(name)
        if type(values)is not list or len(values)>MAX_PAIRS:raise ValueError('Invalid archive correction delta')
        decoded=[]
        for pair in values:
            if type(pair)is not list or len(pair)!=2:raise ValueError('Exact correction endpoint pair required')
            for n in pair:
                if type(n)is not str or not n.isascii()or not n.isdecimal()or len(n)>39 or(len(n)>1 and n[0]=='0'):
                    raise ValueError('Canonical correction integer required')
            decoded.append(tuple(map(int,pair)))
        return set(checked_pairs(decoded,configuration))
    old=set(pairs);added=delta('added');removed=delta('removed')
    if not removed<=old or added&old or added&removed:
        raise ValueError('Scientific correction does not conserve the original pairs')
    repaired=checked_pairs((old-removed)|added,configuration);matches(corrected,repaired)
    updated=copy.deepcopy(frame);scope=presentation(repaired,configuration)
    updated['scopes']['ticket']=scope;updated['record_pairs']=scope['best']
    updated['validation_state']='Scientific source finalized; network audits are separate'
    return corrected,updated,dict(schema='PGN_ARCHIVED_SCIENTIFIC_CORRECTION_V1',marker_sha256=checksum,
        original_report_sha256=original['sha256'],corrected_report_sha256=corrected['sha256'],
        original_pairs=len(old),corrected_pairs=len(repaired))


def verify_archived(folder,body):
    proof=body.get('scientific_finalization')
    if proof is None:return
    from horizon_preseal_queue import PresealQueue,MAX_FILE_BYTES
    if proof.get('handoff_kind')=='audit_count':
        from horizon_audit_partition_archive import completed
        value=completed(folder,body['configuration'],body['report'])
        if value is None or value[2]!=proof.get('audit_receipt_sha256'):
            raise ValueError('Archived audit count handoff changed')
        original=value[0]
    else:original=PresealQueue.read(folder/'preseal-input.json',MAX_FILE_BYTES)['computed']
    _,checksum=marker(folder,body['configuration'],original,body['report'])
    if (proof.get('schema')!='PGN_ARCHIVED_SCIENTIFIC_CORRECTION_V1'
            or proof.get('marker_sha256')!=checksum
            or proof.get('original_report_sha256')!=original['sha256']
            or proof.get('corrected_report_sha256')!=body['report']['sha256']):
        raise ValueError('Archived scientific correction binding changed')
