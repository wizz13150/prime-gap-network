"""Bounded complementary-witness classification and K64 page projection.

This is a private preparation component. A valid factor page is never, by
itself, a certificate of complete pseudoprime enumeration or network admission.
"""
from bisect import bisect_left
from dataclasses import dataclass
import struct

MAX_BATCH_POSITIONS=10**15
MAX_RAW_WITNESSES=262144
MAX_PAGE_WITNESSES=65536
MAX_OWNED_POSITIONS=64000000000000
PAGE_POSITIONS=993*(30<<25)
FACTOR_CUTOFF=10000000
MASK=(1<<64)-1


def exact_integer(value):
    if type(value)is not int or value<0:
        raise ValueError('An exact nonnegative integer is required')
    return value


def euler_jacobi(n):
    if n<3 or not n&1:
        return False
    return pow(2,(n-1)//2,n)==(1 if n%8 in(1,7)else n-1)


@dataclass(frozen=True)
class FactorPage:
    start:int
    length:int
    witness_count:int
    data:bytes
    complete_psp_coverage:bool=False


class RecoveryBatch:
    """Immutable arithmetic records; accepts no completion/admission claim.

    Input is the native producer's little-endian(offset,p,q) stream. Every
    product is checked before filtering or projection; malformed input rejects
    the entire batch. Classification uses independent Python bigint arithmetic.
    """
    __slots__=('_start','_length','_source_count','_rows','_numbers')

    def __setattr__(self,name,value):
        raise AttributeError('Recovery batches are immutable')

    def __init__(self,start,length,raw):
        start,length=exact_integer(start),exact_integer(length)
        if (start<1<<66 or start%30 or not 0<length<=MAX_BATCH_POSITIONS
                or start+length>FACTOR_CUTOFF**3):
            raise ValueError('Scoped residual batch requires class30 origin and N<10^21')
        if (type(raw)is not bytes or len(raw)%24
                or len(raw)>MAX_RAW_WITNESSES*24):
            raise ValueError('Native factor batch exceeds its complete byte envelope')
        seen=set();rows=[]
        for offset,p,q in struct.iter_unpack('<QQQ',raw):
            n=start+offset
            if (offset>=length or not FACTOR_CUTOFF<p<=q or not(p&1 and q&1)
                    or p*q!=n or n in seen):
                raise ValueError('False, duplicate or out-of-range composite witness')
            seen.add(n)
            # Required residual PSPs have prime factors above the cutoff and
            # therefore survive class30. Discarding any other certified product
            # cannot hide a residual PSP; no primary prime boundary is invented.
            if n%2 and n%3 and n%5 and euler_jacobi(n):
                rows.append((n,p,q))
        rows.sort()
        for name,value in(('_start',start),('_length',length),('_source_count',len(raw)//24),
                          ('_rows',tuple(rows)),('_numbers',tuple(r[0]for r in rows))):
            object.__setattr__(self,name,value)

    @property
    def start(self):return self._start
    @property
    def length(self):return self._length
    @property
    def source_count(self):return self._source_count
    @property
    def pseudoprime_count(self):return len(self._rows)
    @property
    def complete_psp_coverage(self):return False

    def pages(self,owned_start,owned_length):
        owned_start,owned_length=exact_integer(owned_start),exact_integer(owned_length)
        if not 0<owned_length<=MAX_OWNED_POSITIONS:
            raise ValueError('The admitted K64 owned-job bound is required')
        end=owned_start+owned_length
        base=owned_start-owned_start%30
        if (base<self._start or end>self._start+self._length
                or owned_start>>64!=(end-1)>>64):
            raise ValueError('Owned job/physical prefix escapes the immutable batch')
        result=[]
        for lo in range(base,end,PAGE_POSITIONS):
            size=min(PAGE_POSITIONS,end-lo)
            a=bisect_left(self._numbers,lo);b=bisect_left(self._numbers,lo+size)
            rows=self._rows[a:b]
            if len(rows)>MAX_PAGE_WITNESSES:
                raise ValueError('Physical page witness budget reached; no truncation')
            data=struct.pack('<8s4Q',b'PGNCMP01',lo&MASK,lo>>64,size,len(rows))
            data+=b''.join(struct.pack('<5Q',n-lo,p,0,q,0)for n,p,q in rows)
            result.append(FactorPage(lo,size,len(rows),data))
        return tuple(result)
