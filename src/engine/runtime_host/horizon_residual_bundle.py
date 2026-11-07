"""Bind one successful owned producer execution to its bounded factor outputs.

Not a publicly verifiable certificate: the caller owns the process receipt and
pins an approved producer image. Completeness is conditional on the primary
having excluded every prime factor up to cutoff; prime-ended sentinels and the
primary computation contract remain separate obligations.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib,json,struct,os


def native_output_argument(folder):
    """One exact native spelling of the same absolute output directory."""
    value=str(Path(folder).resolve())
    if os.name=='nt'and len(value)>=248 and not value.startswith('\\\\?\\'):
        return '\\\\?\\UNC\\'+value[2:]if value.startswith('\\\\')else '\\\\?\\'+value
    return value


@dataclass(frozen=True)
class ResidualBundle:
    cutoff:int
    scopes:tuple
    witnesses:tuple
    producer_sha256:str
    receipt_sha256:str
    witness_sha256:tuple

    @property
    def complete_factor_scan(self):return True

    @property
    def whole95_coverage(self):return False

    @property
    def proves_primary_factor_coverage(self):return False


def bounded_json(file,maximum):
    if (file.is_symlink()or getattr(file.lstat(),'st_file_attributes',0)&0x400
            or file.stat().st_size>maximum):raise ValueError('Residual metadata exceeds path/byte budget')
    with file.open('rb')as stream:raw=stream.read(maximum+1)
    if len(raw)>maximum:raise ValueError('Residual metadata grew beyond its budget')
    return json.loads(raw),hashlib.sha256(raw).hexdigest()


def bind(folder,cutoff,scopes,image_sha256,execution):
    """Accept exact original arguments and successful/drained owned execution only."""
    folder=Path(folder).resolve();scopes=tuple(tuple(pair)for pair in scopes)
    if type(cutoff)is not int or not 2<=cutoff<=10000000 or not 1<=len(scopes)<=64:
        raise ValueError('Residual request limits invalid')
    prior=0;total=0
    for pair in scopes:
        if len(pair)!=2 or any(type(x)is not int for x in pair):raise ValueError('Exact residual bounds required')
        a,b=pair
        if not 3<=a<b<=cutoff**3 or a<prior:raise ValueError('Residual bounds overlap or exceed their proved domain')
        prior=b;total+=b-a
    if total>10**15:raise ValueError('Residual batch exceeds1e15 positions')
    args=execution.get('args')
    expected=[str(cutoff),native_output_argument(folder),*[str(x)for pair in scopes for x in pair]]
    if (not isinstance(args,list)or len(args)!=len(expected)+1 or args[1:]!=expected
            or execution.get('exit')!=0 or execution.get('error')is not None
            or execution.get('all_owned_jobs_drained')is not True or execution.get('GPU_used')is not False):
        raise ValueError('Residual producer needs its exact successful owned execution')
    image=Path(args[0])
    if not isinstance(image_sha256,str)or len(image_sha256)!=64 or hashlib.sha256(image.read_bytes()).hexdigest()!=image_sha256:
        raise ValueError('Residual producer image changed')
    r,fingerprint=bounded_json(folder/'COMPLETE.json',32768)
    if (r.get('schema')!='PGN_SCOPED_SEMIPRIME_PRODUCER_V1' or r.get('wire_format')!='offset_factor_cofactor_u64le'
            or r.get('complete_factor_scan')is not True or r.get('cutoff')!=cutoff
            or r.get('factor_begin')!=cutoff+1 or r.get('factor_end_exclusive')!=__import__('math').isqrt(scopes[-1][1]-1)+1
            or r.get('positions')!=str(total) or r.get('GPU_used')is not False
            or r.get('production_admitted')is not False or r.get('full95_coverage')is not False
            or r.get('prime_ended_coverage_proved')is not False
            or not isinstance(r.get('scopes'),list)or len(r['scopes'])!=len(scopes)):
        raise ValueError('Residual complete receipt differs from the request or coverage contract')
    facts=[];digests=[];seen=0
    for i,(row,(a,b))in enumerate(zip(r['scopes'],scopes)):
        name='scope-'+str(i)+'.factors';n=row.get('witnesses')
        if (row.get('begin')!=str(a)or row.get('end_exclusive')!=str(b)or row.get('file')!=name
                or type(n)is not int or not 0<=n<=262144-seen):raise ValueError('Residual output bounds/count changed')
        seen+=n;file=folder/name
        if (file.is_symlink()or getattr(file.lstat(),'st_file_attributes',0)&0x400 or file.stat().st_size!=24*n):
            raise ValueError('Residual witness file size/path changed')
        values=[];h=hashlib.sha256()
        with file.open('rb')as stream:
            for _ in range(n):
                raw=stream.read(24)
                if len(raw)!=24:raise ValueError('Residual witness stream truncated')
                h.update(raw);offset,p,q=struct.unpack('<QQQ',raw);number=a+offset
                if not a<=number<b or not cutoff<p<=q or p*q!=number:raise ValueError('False residual compositeness witness')
                values.append((number,p,q))
            if stream.read(1):raise ValueError('Residual witness stream grew')
        facts.append(tuple(sorted(set(values))));digests.append(h.hexdigest())
    if seen!=r.get('cofactor_passes'):raise ValueError('Residual aggregate witness count changed')
    return ResidualBundle(cutoff,scopes,tuple(facts),image_sha256,fingerprint,tuple(digests))
