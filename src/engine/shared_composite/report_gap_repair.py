"""Refine a complete retained gap set before report sealing.

This is the report transformation, not a certificate of exhaustive factor
enumeration. Its caller still owes the sparse repair coverage preconditions.
It never edits a session index, sealed report, network state or accepted work.
"""
from dataclasses import dataclass
import copy
import json

from sparse_gap_repair import repair, bounded, integer
from pgn_threshold_policy import verify_pin
from report_commitment import canonical_bytes, digest, validate_computed_report
from pgn_gap_projection import project as project_pairs


@dataclass(frozen=True)
class PreparedReportRepair:
    encoded_report: bytes
    source_sha256: str
    added: tuple
    removed: tuple
    oracle_calls: int

    @property
    def report(self):
        # A fresh value protects the retained transformation from caller edits.
        return json.loads(self.encoded_report)

    @property
    def complete_psp_coverage(self):
        return False


def prepare(report, configuration, primary, factors, covered_start, covered_end, exact_prime):
    """Rebuild exact positional counts and top100 under the original pinned policy.

    primary must contain every retained pair, not just the displayed Best list.
    Matching its projection to the original report catches truncation and stale
    input before any output is returned. Thresholds belong to each lower prime,
    including when a repaired gap crosses a policy boundary or ownership end.
    """
    cfg=copy.deepcopy(configuration)
    a,b=integer(int(cfg['start']),2),integer(int(cfg['end']),2)
    if not a<b or cfg.get('bins',100)!=100:
        raise ValueError('A complete100-partition ticket is required')
    pin=verify_pin(cfg['threshold_policy'],a,b)
    work=dict(start=str(a),length=str(b-a),ticket=cfg['ticket_id'])
    old=validate_computed_report(report,work)
    if old['policy_sha256']!=pin['sha256']:
        raise ValueError('Report policy differs from the original session pin')
    pairs=[]
    for pair in bounded(primary):
        if type(pair)not in(tuple,list)or len(pair)!=2:
            raise ValueError('A complete retained endpoint pair is required')
        lo,hi=(integer(n,2)for n in pair)
        if not a<=lo<b or not lo<hi or lo%2!=1 or (hi-lo)%2:
            raise ValueError('Retained endpoint identity or ownership is invalid')
        pairs.append((lo,hi))
    if len(set(pairs))!=len(pairs):
        raise ValueError('Duplicate retained primary pair')
    pairs.sort()
    if any(left[1]>right[0]for left,right in zip(pairs,pairs[1:])):
        raise ValueError('Retained primary pairs overlap')
    def project(values):
        return project_pairs(values,cfg)
    counts,best=project(pairs)
    if counts!=old['counts'] or best!=old['best']:
        raise ValueError('Retained primary index is incomplete or differs from its report')
    minimum=min(int(s['effective_gap_min'])for s in pin['segments'])
    delta=repair(pairs,factors,a,b,minimum,covered_start,covered_end,exact_prime)
    body=copy.deepcopy(old);body['counts'],body['best']=project(delta.gaps)
    result=dict(body=body,sha256=digest(body),network_accepted=False)
    validate_computed_report(result,work)
    return PreparedReportRepair(canonical_bytes(result),report['sha256'],delta.added,delta.removed,delta.oracle_calls)
