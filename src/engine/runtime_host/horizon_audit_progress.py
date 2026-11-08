"""Present one assigned audit, while its disjoint engine jobs stay internal."""
import datetime as dt
import hashlib
from copy import deepcopy
from decimal import Decimal
from report_commitment import canonical_bytes
from horizon_status_stream import size_label


def presentation(audit, part, folder, title=None):
    ranges = audit['ranges']
    index = ranges.index(part)
    lengths = [int(r['end']) - int(r['start']) for r in ranges]
    if not lengths or min(lengths) <= 0:
        raise ValueError('Invalid assigned audit ranges')
    identity = hashlib.sha256(canonical_bytes(dict(domain='PGN_HORIZON_AUDIT_VIEW_V1',
        request=audit['request_id'], slot=audit['slot'], attempt=audit['attempt']))).hexdigest()
    # Both execution paths visit this assignment in its confirmed range order:
    # NetworkAudits sequentially, or the resident FIFO after prefetched jobs.
    # Offset counts only ranges preceding the active job, never queued work.
    return dict(request_id=audit['request_id'], title=title or 'Audit ' + str(audit['slot'] + 1),
        folder=str(folder), audit_id=identity, audit_slot=audit['slot'], audit_attempt=audit['attempt'],
        offset=str(sum(lengths[:index])), total=str(sum(lengths)),
        part_start=part['start'], part_end=part['end'], part_index=index,
        part_count=len(ranges), final_part=index == len(ranges) - 1)


class WholeAuditPresentation:
    """One audit's bounded result views; replace samples instead of counting twice.

    Only source statistics from the disjoint assigned ranges are added. Missing
    boundary samples carry their last result view, never an invented rate.
    This cache is presentation-only and does not change reports or audit counts.
    """
    def __init__(self):
        self.identity = None
        self.parts = {}
        self.metadata = {}

    def frame(self, item, source=None, *, paused=False):
        if self.identity != item['audit_id']:
            self.identity = item['audit_id']
            self.parts.clear()
            self.metadata.clear()
        value = whole_audit_frame(item, source, paused=paused)
        if source:
            for key in ('bits', 'minimum_gap', 'target_merit'):
                if key in source:self.metadata[key] = source[key]
            scope = source.get('scopes', {}).get('ticket')
            if scope:
                self.parts[item['part_index']] = deepcopy(scope)
        for key, field in self.metadata.items():value.setdefault(key, field)
        if self.parts:
            scopes = dict(value.get('scopes', {}))
            value['scopes'] = scopes
            scopes['ticket'] = self.aggregate()
        return value

    def aggregate(self):
        scopes = list(self.parts.values())
        statistics = [s['statistics'] for s in scopes]
        stats = {}
        for key in ('count_total', 'at_or_above_target'):
            stats[key] = (sum(s[key] for s in statistics)
                          if all(s.get(key) is not None for s in statistics) else None)
        for key in ('threshold_counts', 'merit_band_counts'):
            if any(s.get(key) is None for s in statistics):stats[key] = None;continue
            counts = {}
            for s in statistics:
                for name, count in s[key].items():counts[name] = counts.get(name, 0) + count
            stats[key] = counts
        targets = {s.get('statistics_target') for s in statistics}
        stats['statistics_target'] = next(iter(targets)) if len(targets) == 1 else None
        stats['mixed_targets'] = len(targets) > 1
        def rows(key):
            unique = {}
            for scope in scopes:
                target = scope['statistics'].get('statistics_target')
                for row in scope.get(key, [])[:100]:
                    value = dict(row)
                    if target is not None:value['target_merit'] = target
                    unique[(value['lower_prime'], value['gap'])] = value
            order = ((lambda r:(Decimal(r['merit']), int(r['gap']), -int(r['lower_prime'])))
                     if key == 'best' else (lambda r:int(r['lower_prime'])))
            return sorted(unique.values(), key=order, reverse=True)[:100]
        return dict(statistics=stats, best=rows('best'), recent=rows('recent'))


def whole_audit_frame(item, source=None, *, paused=False):
    """Offset a validated active-job sample; no fabricated contiguous interval.

    Missing first checkpoints retain the exact completed prefix, with no rates.
    A completed internal job is not a completed audit or a network verdict.
    """
    frame = dict(source or {})
    length = int(item['part_end']) - int(item['part_start'])
    partial = int(frame.get('positions', '0'))
    if not 0 <= partial <= length:
        raise ValueError('Audit progress exceeds its active assigned range')
    if source and (str(frame.get('start')) != item['part_start'] or str(frame.get('end')) != item['part_end']):
        raise ValueError('Audit progress belongs to another assigned range')
    done, total = int(item['offset']) + partial, int(item['total'])
    if not 0 <= done <= total:
        raise ValueError('Audit progress exceeds its complete assignment')
    frame.update(schema='PGN_HORIZON_MINING_VIEW_V1', ticket_id=item['audit_id'],
        request_id=item['request_id'], title=item['title'], work_role='audit',
        positions=str(done), positions_total=str(total), positions_label=size_label(total),
        progress_scope='audit', record_pairs=[],
        validation_state='Audit computation; network acceptance remains separate')
    # These coordinates describe the active engine input, not one continuous
    # audit interval. Keep them explicitly scoped, not as whole-audit endpoints.
    frame['audit_active_range'] = dict(start=item['part_start'], end=item['part_end'])
    for key in ('start', 'end', 'current'):
        frame.pop(key, None)
    if not source:
        frame.update(observed_utc=dt.datetime.now(dt.timezone.utc).isoformat(), source_stale=True,
                     source_age_ns=None, speed_rate=None, average_rate=None,
                     compute_tests_rate=None, eta_rate=None)
    complete = done == total and frame.get('phase') == 'Report ready'
    frame['phase'] = 'Paused' if paused else ('Report ready' if complete else 'Searching')
    if paused:
        for key in ('speed_rate', 'average_rate', 'compute_tests_rate', 'eta_rate'):
            frame[key] = None
    return frame
