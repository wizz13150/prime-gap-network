"""Read-only private export of completed local observations and derived tables.

The population is explicitly raw local observations, not corrected/accepted
scientific results. Exact pairs and any finalization delta are kept for later
analysis. No wallet, configuration credentials or whole work folder is copied.
"""
from pathlib import Path
from decimal import Decimal, localcontext, ROUND_FLOOR
import argparse
import csv
import gzip
import hashlib
import io
import json
import os
import re
import sys
import tempfile
import zipfile

PROJECT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(PROJECT/'src/engine/runtime_host'),str(PROJECT/'src/protocol'),str(PROJECT/'tools')]
import horizon_scientific_gaps as science
from horizon_ticket_archive import load as completed_ticket
from export_scientific_results import selected_ticket, _decode, _hash

SCHEMA='PGN_PRIVATE_GAP_OBSERVATIONS_V1'
MAX_TICKETS=64
MAX_ZIP=128*2**20
MAX_TOTAL=512*2**20
MAX_CSV=256*2**20
MAX_KEYS=65536
MAX_RECORDS=2_000_000
POPULATION='raw_local_observations_not_finalized_results'
MEMBER=re.compile(r'(?:observations\.csv|gap-sizes\.csv|merit-bands\.csv|tickets/[0-9a-f]{64}/(?:computed\.json|sealed-report\.json|scientific-gaps\.jsonl\.gz|source-finalization\.json))\Z')


def csv_line(values):
    out=io.StringIO(newline='');csv.writer(out,lineterminator='\n').writerow(values)
    return out.getvalue().encode('utf-8')


def file_chunks(path):
    with path.open('rb')as stream:
        while chunk:=stream.read(1024*1024):yield chunk


def rows(folder,descriptor,configuration):
    """Consume only the previously verified bounded source, then recheck it."""
    path=folder/science.NAME;count=0
    with gzip.open(path,'rb')as stream:
        if _decode(stream.readline(science.MAX_HEADER+1))!=science.header(configuration):
            raise ValueError('Observation header changed during export')
        while True:
            raw=stream.readline(science.MAX_ROW+1)
            if not raw or len(raw)>science.MAX_ROW:raise ValueError('Observation row changed during export')
            value=_decode(raw)
            if type(value)is dict:
                if value!={'records':count}or stream.read(1):raise ValueError('Observation footer changed during export')
                break
            count+=1
            if count>descriptor['records']:raise ValueError('Observation count changed during export')
            yield science.checked_row(value)
    if count!=descriptor['records']or science.digest(path)!=descriptor['sha256']:
        raise ValueError('Observation source changed during export')


def create(output,folders,genesis):
    if not _hash(genesis):raise ValueError('Explicit genesis fingerprint required')
    if not 1<=len(folders)<=MAX_TICKETS:raise ValueError('Select 1 to 64 completed search tickets')
    output=Path(output).absolute();parent=output.parent.resolve(strict=True)
    if output.suffix.lower()!='.zip'or output.exists()or output.is_symlink():
        raise ValueError('A new ZIP output is required; previous exports are never overwritten')
    selected=[];intervals=[];seen=set()
    for supplied in folders:
        ticket,members=selected_ticket(supplied,genesis)
        folder=Path(supplied).resolve(strict=True);body=completed_ticket(folder)
        if body is None or 'scientific_gap_archive'not in body:
            raise ValueError('Exact observations are unavailable; legacy Best/count data cannot restore them')
        if body['frame'].get('work_role')=='audit':
            raise ValueError('Audit work is not a second independent search population')
        if ticket in seen:raise ValueError('Duplicate selected ticket')
        seen.add(ticket);configuration=body['configuration'];a,b=int(configuration['start']),int(configuration['end'])
        if any(a<end and start<b for start,end in intervals):
            raise ValueError('Overlapping search intervals would double-count exposure')
        intervals.append((a,b));descriptor=body['scientific_gap_archive'];science.verify(folder,descriptor,configuration)
        selected.append((folder,body,members,(folder/'session.json').read_bytes()))
    descriptor,temporary=tempfile.mkstemp(prefix='.pgn-gap-export-',suffix='.zip',dir=parent);os.close(descriptor)
    inventory=[];total=0;record_count=0;gap_counts={};merit_counts={}
    try:
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6)as archive:
            def add(name,chunks,limit=MAX_CSV,stored=False):
                nonlocal total
                if not MEMBER.fullmatch(name):raise ValueError('Unexpected observation export member')
                info=zipfile.ZipInfo(name,(1980,1,1,0,0,0));info.compress_type=zipfile.ZIP_STORED if stored else zipfile.ZIP_DEFLATED
                size=0;digest=hashlib.sha256()
                with archive.open(info,'w',force_zip64=True)as target:
                    for chunk in chunks:
                        size+=len(chunk);total+=len(chunk)
                        if size>limit or total>MAX_TOTAL:raise ValueError('Observation export exceeds its byte budget')
                        target.write(chunk);digest.update(chunk)
                        if archive.fp.tell()>MAX_ZIP:raise ValueError('Observation ZIP exceeds its storage budget')
                inventory.append(dict(path=name,bytes=size,sha256=digest.hexdigest()))
            tickets=[]
            for folder,body,members,marker in selected:
                prefix='tickets/'+folder.name+'/';descriptor=body['scientific_gap_archive'];configuration=body['configuration']
                for name,data in sorted(members.items()):add(prefix+name,[data],128*1024)
                add(prefix+science.NAME,file_chunks(folder/science.NAME),science.MAX_FILE,True)
                if inventory[-1]['sha256']!=descriptor['sha256']:raise ValueError('Copied observation source differs')
                if 'scientific_finalization'in body:
                    # completed_ticket already checks this exact immutable correction.
                    path=folder/'source-finalization.json';science.plain(path,8*2**20)
                    with path.open('rb')as stream:data=stream.read(8*2**20+1)
                    if len(data)>8*2**20:raise ValueError('Finalization exceeds its byte budget')
                    if hashlib.sha256(data).hexdigest()!=body['scientific_finalization']['marker_sha256']:
                        raise ValueError('Finalization changed during export')
                    add(prefix+'source-finalization.json',[data],8*2**20)
                tickets.append(dict(ticket_id=folder.name,start=configuration['start'],end=configuration['end'],
                    descriptor=descriptor,collection_policy=configuration['threshold_policy'],
                    genesis_bound_to_sealed_report='sealed-report.json'in members,
                    finalized_correction_included='scientific_finalization'in body))
            def observations():
                nonlocal record_count
                yield csv_line(['ticket_id',*science.COLUMNS,'merit_approx'])
                with localcontext()as context:
                    context.prec=50
                    for folder,body,_,marker in selected:
                        for row in rows(folder,body['scientific_gap_archive'],body['configuration']):
                            lo,_,gap,*_=row;record_count+=1
                            if record_count>MAX_RECORDS:raise ValueError('Observation export exceeds its record budget')
                            merit=Decimal(gap)/Decimal(lo).ln()
                            band=int((merit*10).to_integral_value(rounding=ROUND_FLOOR))
                            gap_counts[gap]=gap_counts.get(gap,0)+1;merit_counts[band]=merit_counts.get(band,0)+1
                            if max(len(gap_counts),len(merit_counts))>MAX_KEYS:raise ValueError('Histogram exceeds its key budget')
                            yield csv_line([folder.name,*[''if v is None else v for v in row],str(merit)])
                        if (folder/'session.json').read_bytes()!=marker:raise ValueError('Completed ticket changed during export')
            add('observations.csv',observations())
            add('gap-sizes.csv',(csv_line(row)for row in [('gap','count'),*sorted(gap_counts.items())]),8*2**20)
            bands=[('merit_lower_inclusive','merit_upper_exclusive','count')]
            bands.extend((str(Decimal(n)/10),str(Decimal(n+1)/10),count)for n,count in sorted(merit_counts.items()))
            add('merit-bands.csv',(csv_line(row)for row in bands),8*2**20)
            manifest=dict(schema=SCHEMA,genesis=genesis,private_backup=True,contains_private_audit_answers=True,
                network_acceptance_proven=False,keys_or_wallets_included=False,population=POPULATION,
                coverage='Selected disjoint intervals only; observations below collection cutoffs are unavailable',
                merit_calculation='gap / ln(lower_prime), Decimal precision50; approximate derived bins of0.1; exact positions retained',
                record_count=record_count,tickets=tickets,members=inventory)
            raw=science.canonical(manifest)+b'\n'
            if len(raw)>2*2**20:raise ValueError('Observation manifest exceeds its budget')
            archive.writestr('manifest.json',raw)
        with open(temporary,'r+b')as stream:os.fsync(stream.fileno())
        proof=verify(temporary,genesis)
        os.link(temporary,parent/output.name)
        return dict(**proof,output=str(parent/output.name))
    finally:Path(temporary).unlink(missing_ok=True)


def verify(path,genesis=None):
    path=Path(path);science.plain(path,MAX_ZIP)
    with zipfile.ZipFile(path)as archive:
        infos=archive.infolist();names=[x.filename for x in infos]
        if len(infos)>MAX_TICKETS*4+4 or len(set(names))!=len(names)or 'manifest.json'not in names:
            raise ValueError('Invalid observation ZIP inventory')
        if archive.getinfo('manifest.json').file_size>2*2**20:raise ValueError('Oversized observation manifest')
        value=_decode(archive.read('manifest.json'))
        if (type(value)is not dict or value.get('schema')!=SCHEMA or value.get('population')!=POPULATION
                or value.get('network_acceptance_proven')is not False or value.get('private_backup')is not True
                or value.get('contains_private_audit_answers')is not True
                or value.get('keys_or_wallets_included')is not False or not _hash(value.get('genesis'))
                or type(value.get('record_count'))is not int or not 0<=value['record_count']<=MAX_RECORDS
                or type(value.get('tickets'))is not list or not 1<=len(value['tickets'])<=MAX_TICKETS
                or(genesis is not None and value['genesis']!=genesis)):
            raise ValueError('Observation backup contract differs')
        entries=value.get('members');total=0
        if type(entries)is not list or len(entries)!=len(infos)-1:raise ValueError('Observation members differ')
        expected=set()
        for entry in entries:
            name=entry['path']
            if type(name)is not str or not MEMBER.fullmatch(name)or name in expected or not _hash(entry['sha256']):
                raise ValueError('Unexpected or duplicate observation member')
            expected.add(name);info=archive.getinfo(name);mode=info.external_attr>>16
            if info.flag_bits&1 or mode&0o170000==0o120000 or type(entry['bytes'])is not int or not 0<=entry['bytes']<=MAX_CSV or info.file_size!=entry['bytes']:
                raise ValueError('Observation member metadata differs')
            total+=info.file_size
            if total>MAX_TOTAL:raise ValueError('Observation ZIP expanded budget exceeded')
            with archive.open(info)as stream:digest=hashlib.file_digest(stream,'sha256').hexdigest()
            if digest!=entry['sha256']:raise ValueError('Observation member checksum differs')
        if expected|{'manifest.json'}!=set(names):raise ValueError('Unlisted observation members')
    return dict(PASS=True,inventory_verified=True,network_acceptance_proven=False,population=POPULATION,
        records=value['record_count'],tickets=len(value['tickets']),bytes=path.stat().st_size,sha256=science.digest(path))


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--genesis',required=True)
    parser.add_argument('--output',type=Path);parser.add_argument('--ticket',action='append',type=Path,default=[])
    parser.add_argument('--verify',type=Path);args=parser.parse_args()
    if args.verify:
        if args.output or args.ticket:parser.error('--verify cannot create an export')
        result=verify(args.verify,args.genesis)
    else:
        if not args.output or not args.ticket:parser.error('--output and explicit --ticket folders required')
        result=create(args.output,args.ticket,args.genesis)
    print(json.dumps(result,sort_keys=True))


if __name__=='__main__':main()
