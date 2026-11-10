"""Local durable audit-count handoff; never a native PASSED/payment receipt."""
import hashlib
import json
from horizon_preparation_process import regular
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes,validate_computed_report

NAME='audit-partition-result.json'
LIMIT=8192


def _validate(value,configuration,computed):
    from horizon_network_audits import partition_ticket
    if (type(value)is not dict or set(value)!={'schema','audit','partition','count','computed_sha256','configuration_sha256'}
            or value['schema']!='PGN_LOCAL_AUDIT_PARTITION_RESULT_V1'):
        raise ValueError('Invalid local audit partition handoff')
    audit,part=value['audit'],value['partition']
    if (type(audit)is not dict or set(audit)!={'request_id','ticket','report_root','slot','attempt','target_merit_q16'}
            or any(type(audit[k])is not str or len(audit[k])!=64 or any(c not in'0123456789abcdef'for c in audit[k])for k in ('request_id','ticket','report_root'))
            or type(audit['slot'])is not int or audit['slot']not in(0,1)
            or type(audit['attempt'])is not int or not 0<=audit['attempt']<1<<32
            or type(audit['target_merit_q16'])is not int or not 0<audit['target_merit_q16']<1<<31
            or type(part)is not dict or set(part)!={'index','start','end'}
            or type(part['index'])is not int or not 0<=part['index']<100
            or any(type(part[k])is not str or not part[k].isascii()or not part[k].isdecimal()
                   or not 1<=len(part[k])<=22 or part[k][0]=='0'for k in ('start','end'))):
        raise ValueError('Invalid local audit assignment context')
    job=partition_ticket(audit,part)
    work=dict(ticket=job['ticket_id'],start=job['start'],length=str(int(job['end'])-int(job['start'])))
    validate_computed_report(computed,work)
    if (configuration['ticket_id']!=job['ticket_id']or configuration['start']!=job['start']or configuration['end']!=job['end']
            or configuration['threshold_policy']!=job['threshold_policy']
            or configuration.get('target_merit')!=audit['target_merit_q16']/65536
            or value['configuration_sha256']!=hashlib.sha256(canonical_bytes(configuration)).hexdigest()
            or value['computed_sha256']!=computed['sha256']or type(value['count'])is not int or not 0<=value['count']<1<<64
            or value['count']!=sum(map(int,computed['body']['counts']))):
        raise ValueError('Local audit count handoff differs from its exact computed partition')


def retain(folder,configuration,audit,part,computed):
    i=next((i for i,p in enumerate(audit['ranges'])if p==part),None)
    if i is None or i>=len(audit['counts']):raise ValueError('Audit count must be recorded before its handoff')
    # The retained result keeps its original Gold display target. The public
    # audit answer commits exact counts/ranges, not that mutable classification.
    # Range, pointwise policy, report root, attempt and payload remain checked
    # by _validate; never rewrite an older handoff to the current Gold target.
    classification=configuration.get('target_merit_q16')
    if (type(classification)is not int or not 0<classification<1<<31
            or configuration.get('target_merit')!=classification/65536):
        raise ValueError('Retained audit classification is not canonical')
    retained_audit=dict(audit,target_merit_q16=classification)
    value=dict(schema='PGN_LOCAL_AUDIT_PARTITION_RESULT_V1',
        audit={k:retained_audit[k]for k in ('request_id','ticket','report_root','slot','attempt','target_merit_q16')},
        partition=dict(part),count=audit['counts'][i],computed_sha256=computed['sha256'],
        configuration_sha256=hashlib.sha256(canonical_bytes(configuration)).hexdigest())
    _validate(value,configuration,computed);raw=canonical_bytes(value)+b'\n'
    if len(raw)>LIMIT:raise BufferError('Local audit partition handoff exceeds its budget')
    file=folder/NAME
    if file.exists():
        if regular(file,LIMIT)!=raw:raise ValueError('Immutable local audit partition handoff changed')
    else:atomic_bytes(file,raw)
    checksum=folder/(NAME+'.sha256');digest=hashlib.sha256(raw).hexdigest().encode()+b'\n'
    if checksum.exists():
        if regular(checksum,65)!=digest:raise ValueError('Audit handoff checksum changed')
    else:atomic_bytes(checksum,digest)


def read(folder,configuration,computed):
    raw=regular(folder/NAME,LIMIT);digest=hashlib.sha256(raw).hexdigest()
    if regular(folder/(NAME+'.sha256'),65)!=digest.encode()+b'\n':raise ValueError('Audit handoff checksum changed')
    _validate(json.loads(raw),configuration,computed)
    return digest


def completed(folder,configuration,report):
    names=(NAME,NAME+'.sha256','source-finalization.json','source-finalization.sha256')
    if not all((folder/n).is_file()for n in names):return None
    from horizon_archive_repair import marker
    original=json.loads(regular(folder/'report.json',65536))
    source=json.loads(regular(folder/'source-finalization.json',8*2**20));corrected=source['corrected_report']
    marker(folder,configuration,original,corrected)
    if report!=original and report!=corrected:raise ValueError('Audit archive report changed')
    digest=read(folder,configuration,corrected)
    return original,corrected,digest
