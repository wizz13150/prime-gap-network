"""Named audit rows and their single native Core anchor, never a verdict source."""
import copy
import json
import re

HASH = re.compile(r'[0-9a-f]{64}\Z')


def identity(value):
    return isinstance(value, str) and bool(HASH.fullmatch(value)) and value != '0' * 64


class AnchoredAuditStates(dict):
    def __init__(self, rows, anchor):
        super().__init__(copy.deepcopy(rows))
        self.anchor = dict(anchor)

    def current(self, rpc):
        return rpc.call('getbestblockhash') == self.anchor['hash']


def read_snapshot(rpc, requests, genesis):
    requested = list(requests)
    if (not 1 <= len(requested) <= 100 or any(not identity(value) for value in requested)
            or len(set(requested)) != len(requested) or not identity(genesis)):
        raise ValueError('One bounded distinct named audit group required')
    value = rpc.call('getpgnauditactivity', [0, len(requested), requested])
    if (not isinstance(value, dict) or value.get('schema') != 'PGN_AUDIT_ACTIVITY_V1'
            or len(json.dumps(value, separators=(',', ':')).encode()) > 524288):
        raise ValueError('Invalid atomic audit activity response')
    state = value.get('state', {})
    if (not isinstance(state, dict) or state.get('genesis') != genesis or not identity(state.get('bestblockhash'))
            or type(state.get('height')) is not int or not 0 <= state['height'] < 1 << 31):
        raise ValueError('Atomic audit activity belongs to another or invalid anchor')
    rows, absent = value.get('audits'), value.get('absent_watched_requests')
    if (not isinstance(rows, list) or not isinstance(absent, list)
            or len(rows) > len(requested) or len(absent) > len(requested)
            or any(not identity(item) for item in absent) or len(set(absent)) != len(absent)):
        raise ValueError('Atomic audit activity does not cover its watched group')
    found, seen = {}, set()
    for row in rows:
        if not isinstance(row, dict):
            raise ValueError('Invalid atomic audit row')
        request = row.get('request_id')
        if not identity(request) or request in seen or request in absent or type(row.get('audited')) is not bool:
            raise ValueError('Different, duplicate or malformed atomic audit request')
        seen.add(request)
        jobs = row.get('jobs')
        publication_v1 = row.get('answer_publication_policy') == 'PGN_AUDIT_BODY_PUBLICATION_V1'
        if not isinstance(jobs, list) or len(jobs) > 2:
            raise ValueError('Invalid atomic audit slots')
        slots = set()
        for job in jobs:
            if (not isinstance(job, dict) or type(job.get('slot')) is not int or job['slot'] not in (0, 1)
                    or job['slot'] in slots or type(job.get('attempt')) is not int
                    or not 0 <= job['attempt'] < 1 << 32
                    or job.get('state') not in ('assigned', 'committed', 'passed', 'failed')
                    or job['state'] != 'assigned' and not identity(job.get('commitment'))):
                raise ValueError('Invalid atomic audit job identity or commitment')
            slots.add(job['slot'])
            if publication_v1 and (type(job.get('answer_published')) is not bool
                                  or job['state'] == 'assigned' and job['answer_published']):
                raise ValueError('Invalid canonical audit body publication state')
        if row['audited'] and (slots != {0, 1} or any(job['state'] != 'passed' for job in jobs)):
            raise ValueError('An audited report requires two passed native slots')
        if request in requested:
            found[request] = row
    if set(found) | set(absent) != set(requested):
        raise ValueError('Atomic audit activity omitted or added watched requests')
    if rpc.call('getblockhash', [state['height']]) != state['bestblockhash']:
        return None  # A real reorganization invalidates this exact snapshot.
    return AnchoredAuditStates(found, dict(genesis=genesis, hash=state['bestblockhash'], height=state['height']))
