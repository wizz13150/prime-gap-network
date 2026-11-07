"""Project a bound residual execution into exact, immutable native factor pages.

This is mathematical input preparation. It neither certifies the primary
engine's small-factor coverage nor authorizes a network report.
"""
from dataclasses import dataclass
from pathlib import Path
import hashlib,json,os,struct
from psp_batch import RecoveryBatch,euler_jacobi
from horizon_residual_bundle import ResidualBundle
from pgn_session import SessionControllerLock,folder_lock


class CoverageExtensionPending(RuntimeError):
    pass


MAX_CACHE_DIRECTORIES=256
MAX_CACHE_FILES=4096
MAX_CACHE_BYTES=128*2**20


def _regular(path):
    info=path.lstat()
    return path.is_file()and not path.is_symlink()and not getattr(info,'st_file_attributes',0)&0x400


def _read(path,maximum):
    if not _regular(path)or path.stat().st_size>maximum:
        raise ValueError('Complement cache file exceeds its path/byte envelope')
    with path.open('rb')as stream:raw=stream.read(maximum+1)
    if len(raw)>maximum:raise ValueError('Complement cache file grew beyond its envelope')
    return raw


def _publish_staged(path,data):
    # Only unpublished, deterministic files may be resumed. A short exact prefix
    # is a torn write; an altered byte is corruption and must never be blessed.
    if path.exists():
        before=_read(path,len(data))
        if before==data:return
        if not data.startswith(before):raise ValueError('Unpublished complement bytes changed')
        mode='wb'
    else:mode='xb'
    with path.open(mode)as stream:
        stream.write(data);stream.flush();os.fsync(stream.fileno())


@dataclass(frozen=True)
class ComplementFeed:
    batch:RecoveryBatch
    fingerprint:str
    extension_checks:tuple

    @property
    def primary_factor_cutoff_required(self):return 10000000

    @property
    def complete_psp_coverage(self):return False


@dataclass(frozen=True)
class ComplementFeedBank:
    """Bounded immutable routing across prepared producer scopes.

    This collection does not upgrade the evidence carried by its feeds. A
    caller must merge overlapping source scopes before running the producer;
    selecting an arbitrary overlapping feed would hide an ambiguous binding.
    """
    feeds:tuple

    def __post_init__(self):
        if (type(self.feeds)is not tuple or not 1<=len(self.feeds)<=64
                or any(not isinstance(feed,ComplementFeed)for feed in self.feeds)):
            raise ValueError('One through64 prepared complementary feeds required')
        ordered=tuple(sorted(self.feeds,key=lambda feed:feed.batch.start))
        previous=None
        for feed in ordered:
            a,b=feed.batch.start,feed.batch.start+feed.batch.length
            if previous is not None and a<previous:
                raise ValueError('Complement feed scopes overlap; merge before preparation')
            previous=b
        object.__setattr__(self,'feeds',ordered)

    def select(self,start,end):
        if type(start)is not int or type(end)is not int or not start<end:
            raise ValueError('Exact nonempty complementary work bounds required')
        for feed in self.feeds:
            if feed.batch.start<=start<end<=feed.batch.start+feed.batch.length:
                return feed
        raise ValueError('Work escapes the prepared complementary scopes')


def prepare(bundle,scope_index,exact_prime):
    if not isinstance(bundle,ResidualBundle)or bundle.cutoff!=10000000:
        raise ValueError('Bound residual producer throughcutoff1e7 required')
    if type(scope_index)is not int or not 0<=scope_index<len(bundle.scopes):
        raise ValueError('Residual scope index invalid')
    a,b=bundle.scopes[scope_index];start=a-a%30
    checks=[]
    # Legacy scoped scans may begin at the proved left prime instead of its
    # class30 floor. Independently close only this <30-integer extension; no
    # silent padding of an unenumerated region is accepted.
    for n in range(start,a):
        if n%2 and n%3 and n%5 and euler_jacobi(n):
            decision=exact_prime(n)
            if type(decision)is not bool:raise ValueError('Exact prime decision required')
            if not decision:raise CoverageExtensionPending('Class30 extension contains an unaccounted Euler-Jacobi composite; retain work and supply its witness')
            checks.append((str(n),'proved_prime'))
    records={}
    for n,p,q in bundle.witnesses[scope_index]:
        if not a<=n<b or not 10000000<p<=q or p*q!=n:raise ValueError('Residual compositeness product/ownership changed')
        # Multiple valid factorizations can concern the same number. Retain one
        # deterministic certificate; every number remains represented.
        records[n]=min(records.get(n,(p,q)),(p,q))
    raw=b''.join(struct.pack('<QQQ',n-start,p,q)for n,(p,q)in sorted(records.items()))
    batch=RecoveryBatch(start,b-start,raw)
    identity=dict(schema='PGN_BOUND_COMPLEMENT_FEED_V1',scope=scope_index,original_bounds=[str(a),str(b)],
        aligned_bounds=[str(start),str(b)],producer_sha256=bundle.producer_sha256,
        receipt_sha256=bundle.receipt_sha256,witness_sha256=bundle.witness_sha256[scope_index],
        rebased_sha256=hashlib.sha256(raw).hexdigest(),extension_checks=checks)
    encoded=json.dumps(identity,sort_keys=True,separators=(',',':')).encode()
    return ComplementFeed(batch,hashlib.sha256(encoded).hexdigest(),tuple(checks))


def publish_pages(feed,cache,owned_start,owned_length):
    if not isinstance(feed,ComplementFeed):raise ValueError('Prepared immutable feed required')
    pages=feed.batch.pages(owned_start,owned_length)
    descriptors=[dict(name='page-'+str(i)+'.pgncmp',start=str(page.start),length=page.length,
        witnesses=page.witness_count,bytes=len(page.data),sha256=hashlib.sha256(page.data).hexdigest())for i,page in enumerate(pages)]
    body=dict(schema='PGN_NATIVE_COMPLEMENT_PAGES_V1',feed=feed.fingerprint,owned_start=str(owned_start),
        owned_length=str(owned_length),primary_cutoff_required=10000000,pages=descriptors,network_admitted=False)
    encoded=json.dumps(body,sort_keys=True,separators=(',',':')).encode()
    name=hashlib.sha256(encoded).hexdigest();cache=Path(cache)
    if cache.exists()and(cache.is_symlink()or getattr(cache.lstat(),'st_file_attributes',0)&0x400):raise ValueError('Complement cache cannot be a reparse point')
    cache.mkdir(parents=True,exist_ok=True);cache=cache.resolve();final=cache/name
    with folder_lock(cache):
      lock=SessionControllerLock(cache)
      try:
        if final.exists():
            if final.is_symlink()or getattr(final.lstat(),'st_file_attributes',0)&0x400:raise ValueError('Complement page directory changed')
            marker=final/'manifest.json'
            if _read(marker,32768)!=encoded+b'\n':
                raise ValueError('Immutable complement manifest changed')
            for row in descriptors:
                p=final/row['name']
                if hashlib.sha256(_read(p,row['bytes'])).hexdigest()!=row['sha256']:
                    raise ValueError('Immutable complement page changed')
            return final
        files=0;size=0;directories=0
        for folder in cache.iterdir():
            if folder.is_dir():
                if folder.is_symlink()or getattr(folder.lstat(),'st_file_attributes',0)&0x400:raise ValueError('Complement cache contains a reparse point')
                directories+=1
                for p in folder.iterdir():
                    if not p.is_file()or p.is_symlink()or getattr(p.lstat(),'st_file_attributes',0)&0x400:raise ValueError('Unexpected complement cache entry')
                    files+=1;size+=p.stat().st_size
        temporary=cache/('.pending-'+name)
        expected={row['name']:page.data for row,page in zip(descriptors,pages)}
        expected['manifest.json']=encoded+b'\n'
        present={}
        if temporary.exists():
            if not temporary.is_dir()or temporary.is_symlink()or getattr(temporary.lstat(),'st_file_attributes',0)&0x400:
                raise ValueError('Unpublished complement directory changed')
            for p in temporary.iterdir():
                if p.name not in expected:raise ValueError('Unexpected unpublished complement entry')
                raw=_read(p,len(expected[p.name]))
                if not expected[p.name].startswith(raw):raise ValueError('Unpublished complement bytes changed')
                present[p.name]=len(raw)
        extra_files=len(expected)-len(present)
        extra_bytes=sum(len(data)-present.get(n,0)for n,data in expected.items())
        if (directories+int(not temporary.exists())>MAX_CACHE_DIRECTORIES
                or files+extra_files>MAX_CACHE_FILES or size+extra_bytes>MAX_CACHE_BYTES):
            raise BufferError('Complement cache quota reached; retained proof/work is not deleted')
        temporary.mkdir(exist_ok=True)
        for n,data in expected.items():_publish_staged(temporary/n,data)
        os.replace(temporary,final)
        return final
      finally:lock.release()
