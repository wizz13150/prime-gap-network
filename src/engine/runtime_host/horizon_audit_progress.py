"""Present one assigned audit, while its disjoint engine jobs stay internal."""
import datetime as dt
import hashlib
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
        part_start=part['start'], part_end=part['end'], final_part=index == len(ranges) - 1)


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
