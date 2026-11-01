"""Exact integer transition construction for Robert's retained-merit rule.

Existing session pins are immutable. This rule is selected explicitly for a new pin.
Decimal ln/exp are correctly rounded; neighboring representable values enclose
the real result. Precision increases until the integer decision is unambiguous.
"""
from dataclasses import dataclass
from decimal import Decimal,localcontext,ROUND_CEILING,ROUND_FLOOR,ROUND_HALF_EVEN
from functools import lru_cache

DOMAIN_START=1<<66
DOMAIN_END=1<<95
MINIMUM_MERIT='20.5'

def _merit(value):
    d=Decimal(str(value))
    if not d.is_finite()or d<=0:raise ValueError('Positive finite retained merit required')
    return d

@lru_cache(maxsize=4096)
def minimum_gap(number,merit=MINIMUM_MERIT):
    number=int(number)
    if number<=1:raise ValueError('Search integer must exceed one')
    m=_merit(merit)
    for precision in(50,100,200,400,800):
        with localcontext()as context:
            context.prec=precision;context.rounding=ROUND_HALF_EVEN
            logarithm=Decimal(number).ln()
            lower=(((logarithm.next_minus()*m).next_minus())/2).next_minus()
            upper=(((logarithm.next_plus()*m).next_plus())/2).next_plus()
            lo=2*int(lower.to_integral_value(rounding=ROUND_CEILING))
            hi=2*int(upper.to_integral_value(rounding=ROUND_CEILING))
            if lo==hi:return lo
    raise ArithmeticError('Retained-gap boundary needs more precision; no rounded guess accepted')

@lru_cache(maxsize=4096)
def next_threshold_integer(gap,merit=MINIMUM_MERIT):
    gap=int(gap)
    if gap<=0 or gap%2:raise ValueError('Positive even gap threshold required')
    m=_merit(merit)
    for precision in(50,100,200,400,800):
        with localcontext()as context:
            context.prec=precision;context.rounding=ROUND_HALF_EVEN
            exponent=Decimal(gap)/m
            lower=exponent.next_minus().exp().next_minus()
            upper=exponent.next_plus().exp().next_plus()
            lo=int(lower.to_integral_value(rounding=ROUND_FLOOR))
            hi=int(upper.to_integral_value(rounding=ROUND_FLOOR))
            if lo==hi:return lo+1
    raise ArithmeticError('Transition integer needs more precision; no rounded guess accepted')

@dataclass(frozen=True)
class LogicalPiece:
    start:int
    end:int
    bits:int
    high_word:int
    minimum_gap:int

def next_piece(start,end,maximum_positions,merit=MINIMUM_MERIT):
    start,end,maximum_positions=map(int,(start,end,maximum_positions))
    if not DOMAIN_START<=start<end<=DOMAIN_END:raise ValueError('67–95-bit logical range required')
    if maximum_positions<=0:raise ValueError('Positive physical-piece limit required')
    gap=minimum_gap(start,merit);bits=start.bit_length();high=start>>64
    stop=min(end,start+maximum_positions,1<<bits,(high+1)<<64,next_threshold_integer(gap,merit))
    if stop<=start:raise ArithmeticError('Logical planner failed to advance')
    return LogicalPiece(start,stop,bits,high,gap)

def pieces(start,end,maximum_positions,merit=MINIMUM_MERIT):
    while start<end:
        piece=next_piece(start,end,maximum_positions,merit)
        yield piece
        start=piece.end
