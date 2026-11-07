"""Separate CPU repair of false separators; never a coverage certificate.

Conditional completeness lemma: let P be true primes and S the primary's
accepted boundaries, with P contained in S. Every adjacent P pair is either
already adjacent in S or contains a member of S-P. Exact prime neighbors of
all such false separators, plus unchanged true primary pairs, therefore give
all qualifying true gaps. Ownership belongs to the lower prime.

The caller must supply an exhaustive false-separator list and an exact proved
prime predicate. This component does not establish either precondition, change
network admission, or recover an aborted/incomplete primary execution.
"""
from dataclasses import dataclass

MAX_ITEMS=65536
MAX_ORACLE_CALLS=1000000
# Native index integers are128bit. A95bit owned range may have its actual
# upper prime just beyond2^95; do not truncate that verification halo.
MAX_INTEGER=(1<<128)-1

class RepairBudgetExceeded(BufferError):
    pass

def integer(value,minimum=0):
    if type(value)is not int or not minimum<=value<=MAX_INTEGER:
        raise ValueError('An exact bounded integer is required')
    return value

def bounded(values):
    for index,value in enumerate(values):
        if index>=MAX_ITEMS:raise RepairBudgetExceeded('Repair item budget reached')
        yield value

@dataclass(frozen=True)
class SparseRepair:
    gaps:tuple
    added:tuple
    removed:tuple
    reconstructed:tuple
    oracle_calls:int

    @property
    def complete_psp_coverage(self):return False

def repair(primary,factors,owned_start,owned_end,minimum_gap,covered_start,covered_end,
           exact_prime,*,maximum_oracle_calls=MAX_ORACLE_CALLS):
    """Return an atomic immutable correction, bounded by proved prime endpoints.

    factors contains (n,p,q), where p*q=n and p,q>1. Factors themselves need
    not be prime: their exact product is sufficient compositeness evidence.
    covered_start/end are inclusive proven prime sentinels enclosing ownership.
    The prepared factor scope must include the entire sentinel interval, even
    when a false boundary lies beyond the logical ownership end.
    """
    a,b,first,last=(integer(x,2)for x in(owned_start,owned_end,covered_start,covered_end))
    minimum_gap=integer(minimum_gap,2)
    if not first<=a<b<=last:raise ValueError('Prime-ended coverage must enclose ownership')
    if type(maximum_oracle_calls)is not int or not 1<=maximum_oracle_calls<=MAX_ORACLE_CALLS:
        raise ValueError('An exact bounded oracle budget is required')
    old=set()
    for item in bounded(primary):
        if type(item)not in(tuple,list)or len(item)!=2:raise ValueError('Gap pair required')
        lo,hi=(integer(x,2)for x in item)
        if not a<=lo<b or hi>last or hi-lo<minimum_gap or(lo,hi)in old:
            raise ValueError('Primary gap identity, ownership or threshold is invalid')
        old.add((lo,hi))
    ordered=sorted(old)
    if any(left[1]>right[0]for left,right in zip(ordered,ordered[1:])):
        raise ValueError('Primary gap pairs overlap')
    false=set()
    for item in bounded(factors):
        if type(item)not in(tuple,list)or len(item)!=3:raise ValueError('Exact factor triple required')
        n,p,q=(integer(x,2)for x in item)
        if p*q!=n or not first<n<last:raise ValueError('False or uncovered compositeness witness')
        false.add(n)
    calls=0
    def prime(n):
        nonlocal calls
        # Exact cheap compositeness certificates precede the costly oracle.
        if n>5 and (n%2==0 or n%3==0 or n%5==0):return False
        if calls>=maximum_oracle_calls:raise RepairBudgetExceeded('Proved-neighbor oracle budget reached; no partial result')
        calls+=1;value=exact_prime(n)
        if type(value)is not bool:raise ValueError('Exact prime predicate returned no boolean decision')
        return value
    if not prime(first)or not prime(last):raise ValueError('Coverage endpoints must be proved primes')
    intervals=[]
    for n in sorted(false):
        if intervals and intervals[-1][0]<n<intervals[-1][1]:continue
        lo=n-1;hi=n+1
        while not prime(lo):lo-=1
        while not prime(hi):hi+=1
        if lo<first or hi>last:raise ValueError('Neighbor walk escaped proved coverage')
        intervals.append((lo,hi))
    removed={g for g in old if g[0]in false or g[1]in false}
    # A primary gap with genuine endpoints cannot partially cross another
    # certified true gap. Reject contradictory evidence instead of hiding it.
    index=0
    for g in ordered:
        if g in removed:continue
        while index<len(intervals)and intervals[index][1]<=g[0]:index+=1
        if index<len(intervals):
            span=intervals[index]
            if g[0]<span[1]and g[1]>span[0]and g!=span:
                raise ValueError('Primary gap crosses a proved prime inside repaired coverage')
    new=(old-removed)|{g for g in intervals if a<=g[0]<b and g[1]-g[0]>=minimum_gap}
    return SparseRepair(tuple(sorted(new)),tuple(sorted(new-old)),tuple(sorted(old-new)),tuple(intervals),calls)
