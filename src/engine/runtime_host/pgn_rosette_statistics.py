"""Presentation counters using the unchanged, unrounded per-ticket tiers.

These are discoveries, not audit acceptance, awarded coins or new search rules.
"""
import math

ROSETTE_KEYS=('gold','silver','bronze','rust','ordinary')

def empty_rosettes():
    return dict.fromkeys(ROSETTE_KEYS,0)

def add_rosette(counts,merit,target,delta=1):
    if not math.isfinite(merit) or merit<0 or not math.isfinite(target) or target<=0:
        raise ValueError('Invalid presentation merit or ticket target')
    key=next((name for offset,name in enumerate(ROSETTE_KEYS[:4]) if merit>=target-offset),'ordinary')
    counts[key]+=delta
    if counts[key]<0:
        raise ValueError('Rosette presentation counter moved below zero')
