"""Bounded exact endpoint projection shared by report repair and archiving."""
from bisect import bisect_right
import math
from pgn_threshold_policy import at_position, verify_pin

MAX_PAIRS=65536


def checked_pairs(values,configuration):
    a,b=int(configuration['start']),int(configuration['end'])
    if not 2<=a<b<1<<128 or configuration.get('bins',100)!=100:
        raise ValueError('An exact bounded100-partition range is required')
    verify_pin(configuration['threshold_policy'],a,b)
    pairs=[]
    for i,item in enumerate(values):
        if i>=MAX_PAIRS:raise BufferError('Complete gap projection exceeds its item budget')
        if type(item)not in(tuple,list)or len(item)!=2:
            raise ValueError('Exact endpoint pair required')
        lo,hi=item
        if (type(lo)is not int or type(hi)is not int or not a<=lo<b
                or not lo<hi<1<<128 or lo%2!=1 or(hi-lo)%2):
            raise ValueError('Gap identity or lower-prime ownership changed')
        pairs.append((lo,hi))
    pairs.sort()
    if len(set(pairs))!=len(pairs)or any(x[1]>y[0]for x,y in zip(pairs,pairs[1:])):
        raise ValueError('Duplicate or overlapping retained gap pairs')
    return tuple(pairs)


def project(values,configuration):
    pairs=checked_pairs(values,configuration)
    a,b=int(configuration['start']),int(configuration['end'])
    bounds=[a+(b-a)*i//100 for i in range(101)]
    counts=[0]*100;detail=[]
    for lo,hi in pairs:
        segment=at_position(configuration['threshold_policy'],lo);gap=hi-lo
        if gap>=int(segment['count_gap']):counts[bisect_right(bounds,lo)-1]+=1
        if gap>=int(segment['detail_gap']):detail.append((lo,hi))
    best=[dict(gap=hi-lo,lower_prime=str(lo),upper_prime=str(hi))
          for lo,hi in sorted(detail,key=lambda g:(g[0]-g[1],g[0]))[:100]]
    return [str(n)for n in counts],best


def presentation(values,configuration):
    pairs=checked_pairs(values,configuration)
    count=int(configuration.get('count_gap',configuration.get('min_gap',960)))
    first=((count+99)//100)*100
    thresholds=sorted({count,*[v for v in range(first,first+600,100)if v>count]})
    target=float(configuration.get('target_merit',32))
    if not math.isfinite(target)or target<=0:raise ValueError('Invalid statistics target')
    stats=dict(count_total=0,threshold_counts={str(t):0 for t in thresholds},
               merit_band_counts={},at_or_above_target=0,statistics_target=target)
    counted=[]
    def row(pair):
        lo,hi=pair
        return dict(gap=hi-lo,lower_prime=str(lo),upper_prime=str(hi),merit=format((hi-lo)/math.log(lo),'.17g'))
    for lo,hi in pairs:
        gap=hi-lo
        for t in thresholds:
            if gap>=t:stats['threshold_counts'][str(t)]+=1
        if gap>=int(at_position(configuration['threshold_policy'],lo)['count_gap']):
            counted.append((lo,hi));stats['count_total']+=1
            merit=gap/math.log(lo)
            if merit>=target:stats['at_or_above_target']+=1
            else:
                key=str(math.floor(merit));stats['merit_band_counts'][key]=stats['merit_band_counts'].get(key,0)+1
    # Completed repaired history is ordered by logical position, never given
    # fabricated discovery times for gaps reconstructed after GPU completion.
    return dict(statistics=stats,best=[row(p)for p in sorted(counted,key=lambda g:(g[0]-g[1],g[0]))[:100]],
                recent=[row(p)for p in reversed(counted[-30:])],recent_order='lower_prime_descending')
