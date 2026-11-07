"""Owned CPU work issued by the logical miner's existing normal work cycle.

No network allocation, user role, proof promotion, or ticket/lease policy lives
here. A CPU result with pending source coverage is evidence, never a report.
"""
from contracts import integer

SCHEMA = 'PGN_CPU_CYCLE_WORK_V1'
RECORDS = 'canonical_reported_gap_and_exact_seams'

def hex_identity(value):
    if type(value) is not str or len(value) != 64 or any(c not in '0123456789abcdef' for c in value):
        raise ValueError('Canonical logical-miner/cycle identity required')
    return value

def partitions(request):
    required = {'schema','work_cycle','normal_ticket','stage','ticket_id','start','end'}
    if type(request) is not dict or set(request) != required or request['schema'] != SCHEMA:
        raise ValueError('Exact normal-cycle owned work fields required')
    for key in ('work_cycle','normal_ticket','ticket_id'):
        hex_identity(request[key])
    if request['stage'] not in ('audit1','audit2','search'):
        raise ValueError('CPU work must belong to a normal Audit1/Audit2/search cycle')
    lo, hi = integer(request['start']), integer(request['end'])
    if not (1 << 67) <= lo < hi <= (1 << 68):
        raise ValueError('Owned68 only; CPU source coverage outside this domain is unsupported')
    if not 100 <= hi-lo <= 200000000000000:
        raise ValueError('Normal owned work must have 100..2e14 positions')
    if (hi-lo)%100:
        raise ValueError('CPU counts100 require100 equal partitions; issued tickets and audit partitions satisfy this contract')
    return [dict(index=i, start=str(lo+(hi-lo)*i//100), end=str(lo+(hi-lo)*(i+1)//100)) for i in range(100)]

def records(value, start, end, count, minimum):
    if type(value) is not list or len(value) != min(100,count):
        raise ValueError('Every piece must return its complete bounded top100')
    out=[]
    for item in value:
        if type(item) is not dict or set(item) != {'lower_prime','upper_prime','gap'}:
            raise ValueError('Exact bounded gap record required')
        lo,hi=integer(item['lower_prime']),integer(item['upper_prime'])
        gap=item['gap']
        if type(gap) is not int or gap<=0 or not start <= lo < end or hi-lo != gap or gap < minimum or gap % 2:
            raise ValueError('Gap record does not belong to its owned lower-prime interval')
        out.append(dict(lower_prime=str(lo),upper_prime=str(hi),gap=gap))
    ordered=sorted(out,key=lambda x:(-x['gap'],int(x['lower_prime'])))
    if out != ordered or len({x['lower_prime'] for x in out}) != len(out):
        raise ValueError('Bounded best must be unique and canonical gap-desc/lower-asc')
    return out

def merge_best(left, right):
    found={x['lower_prime']:x for x in left}
    for item in right:
        old=found.get(item['lower_prime'])
        if old is not None and old != item:
            raise ValueError('Conflicting prime-gap identity')
        found[item['lower_prime']]=item
    return sorted(found.values(),key=lambda x:(-x['gap'],int(x['lower_prime'])))[:100]

def frontier(parts, committed):
    """Explicit coverage: unvisited counts are not advertised as full-bin zeros."""
    if type(committed)is not int or not 0<=committed<=sum(integer(p['end'])-integer(p['start'])for p in parts):
        raise ValueError('CPU frontier outside assigned work')
    remaining=committed; rows=[]
    for part in parts:
        lo,hi=integer(part['start']),integer(part['end']); used=min(remaining,hi-lo)
        rows.append(dict(index=part['index'],start=str(lo),end=str(hi),covered_end=str(lo+used),complete=used==hi-lo))
        remaining-=used
    if remaining: raise ValueError('CPU frontier exceeds assigned work')
    return rows
