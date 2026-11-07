"""Bound source preparation, including exact prime-ended halos, before execution.

Planning does not allocate network work or claim a producer has run. Deferred
work retains its identity. Covered positions, including halos, consume budget.
"""
from dataclasses import dataclass
import re

LOW=1<<66
DOMAIN=10**21
MAX_POSITIONS=10**15


class PreparationHaloPending(RuntimeError):
    pass


@dataclass(frozen=True)
class PlannedWork:
    work_id:str
    start:int
    end:int
    left_prime:int
    right_prime:int


@dataclass(frozen=True)
class PreparationPlan:
    selected:tuple
    deferred:tuple
    scopes:tuple

    @property
    def covered_positions(self):return sum(b-a for a,b in self.scopes)

    def document(self):
        selected=[]
        for work in self.selected:
            matches=[i for i,(a,b)in enumerate(self.scopes)
                     if a<=work.left_prime<=work.start<work.end<=work.right_prime<b]
            if len(matches)!=1:raise ValueError('Preparation work has an ambiguous scope binding')
            selected.append(dict(work_id=work.work_id,start=str(work.start),end=str(work.end),
                                 left_prime=str(work.left_prime),right_prime=str(work.right_prime),scope=matches[0]))
        return dict(schema='PGN_COMPLEMENT_PREPARATION_PLAN_V1',selected=selected,
                    deferred=list(self.deferred),scopes=[dict(start=str(a),end=str(b))for a,b in self.scopes],
                    covered_positions=str(self.covered_positions),producer_executed=False,network_admitted=False)


def _union(work):
    result=[]
    for a,b in sorted((w.left_prime//30*30,w.right_prime+1)for w in work):
        if result and a<=result[-1][1]:result[-1]=(result[-1][0],max(b,result[-1][1]))
        else:result.append((a,b))
    return tuple(result)


def plan_preparation(requests,exact_prime,*,maximum_positions=MAX_POSITIONS,halo_limit=1<<20):
    if (type(requests)not in(list,tuple)or not 1<=len(requests)<=64
            or type(maximum_positions)is not int or not 1<=maximum_positions<=MAX_POSITIONS
            or type(halo_limit)is not int or not 0<=halo_limit<=1<<20
            or not callable(exact_prime)):
        raise ValueError('Bounded preparation request, position budget and exact oracle required')
    seen=set()
    for work in requests:
        if (type(work)is not dict or set(work)!={'work_id','start','end'}
                or type(work['work_id'])is not str or not re.fullmatch('[0-9a-f]{64}',work['work_id'])
                or work['work_id']in seen or type(work['start'])is not int or type(work['end'])is not int
                or not LOW<=work['start']<work['end']<=DOMAIN):
            raise ValueError('Distinct work identity and exact supported logical bounds required')
        seen.add(work['work_id'])

    def seek(n,direction):
        for distance in range(halo_limit+1):
            value=n+direction*distance
            if not 2<=value<DOMAIN:break
            if value>2 and value%2==0:continue
            answer=exact_prime(value)
            if type(answer)is not bool:raise ValueError('Exact boolean prime decision required')
            if answer:return value
        raise PreparationHaloPending('Prime-ended halo is not proved inside the preparation budget')

    selected=[];deferred=[]
    for work in requests:
        candidate=PlannedWork(work['work_id'],work['start'],work['end'],
                              seek(work['start'],-1),seek(work['end'],1))
        scopes=_union([*selected,candidate])
        if sum(b-a for a,b in scopes)<=maximum_positions:selected.append(candidate)
        else:deferred.append(work['work_id'])
    return PreparationPlan(tuple(selected),tuple(deferred),_union(selected))
