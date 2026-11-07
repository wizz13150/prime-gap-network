"""Export bounded, public PGN testnet observations without changing mining or consensus.

Report commitment time is not authenticated computation time. This export keeps
claimed and audited volumes distinct and never invents a zero capacity estimate.
It does not open a wallet or export report answers, credentials or private keys.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import re
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'src/engine/runtime_host'))
from horizon_core_rpc import CoreRpc

IDENTITY = re.compile(r'^[0-9a-f]{64}$')
DECIMAL = re.compile(r'^(0|[1-9][0-9]{0,38})$')


def integer(value):
    if type(value) is not int or value < 0:
        raise ValueError('Nonnegative integer metadata required')
    return value


def positions(value):
    if not isinstance(value, str) or not DECIMAL.fullmatch(value):
        raise ValueError('Canonical exact position string required')
    return int(value)


def validate(snapshot, genesis, maximum_points):
    if snapshot.get('schema') != 'PGN_NETWORK_METRICS_V1' or snapshot.get('genesis') != genesis:
        raise ValueError('Unknown metrics schema or foreign chain')
    if not IDENTITY.fullmatch(snapshot.get('bestblockhash', '')):
        raise ValueError('Canonical snapshot tip required')
    if snapshot.get('rate_scope') != 'reported_search_positions_per_chain_second_excludes_audits':
        raise ValueError('Source volume must exclude audits and shadow computation')
    if snapshot.get('audit_validation_scope') != 'historical_reports_qualified_at_snapshot_tip':
        raise ValueError('Unknown historical qualification semantics')
    for key in ('height', 'median_time_past', 'window_seconds', 'span_seconds',
                'report_count', 'audited_report_count', 'audit_enabled_operators',
                'reporting_operators_in_window'):
        integer(snapshot[key])
    if snapshot['audited_report_count'] > snapshot['report_count']:
        raise ValueError('Audited report count exceeds reported count')
    if positions(snapshot['audited_positions']) > positions(snapshot['reported_positions']):
        raise ValueError('Audited volume exceeds reported volume')
    points = snapshot.get('points')
    if not isinstance(points, list) or len(points) > maximum_points:
        raise ValueError('Metrics exceeded the requested point budget')
    previous_time = -1
    for point in points:
        for key in ('height', 'merit_target_q16', 'gold_count', 'window_seconds'):
            integer(point[key])
        current_time = integer(point['time'])
        if current_time < previous_time or integer(point['height']) > snapshot['height']:
            raise ValueError('Nonchronological or future metrics point')
        previous_time = current_time
        if not IDENTITY.fullmatch(point.get('hash', '')):
            raise ValueError('Canonical metrics point hash required')
        if positions(point['audited_positions']) > positions(point['reported_positions']):
            raise ValueError('Audited point volume exceeds reported volume')
    queue = snapshot['audit_queue']
    public_queue = {}
    for key in ('awaiting', 'assigned', 'committed', 'passed', 'failed', 'awaiting_seed'):
        public_queue[key] = integer(queue[key])
    public_queue['compute_positions_pending'] = str(positions(queue['compute_positions_pending']))
    age = queue['oldest_compute_pending_age_seconds']
    public_queue['oldest_compute_pending_age_seconds'] = None if age is None else integer(age)
    public_keys = ('schema', 'genesis', 'bestblockhash', 'height', 'median_time_past',
                   'rate_scope', 'audit_validation_scope', 'window_seconds', 'span_seconds',
                   'report_count', 'audited_report_count', 'reported_positions', 'audited_positions',
                   'reporting_operators_in_window', 'audit_enabled_operators')
    public_points = ('time', 'height', 'hash', 'merit_target_q16', 'gold_count', 'window_seconds',
                     'reported_positions', 'audited_positions')
    # Unknown future fields cannot smuggle wallet data or audit answers into an export.
    return dict({key: snapshot[key] for key in public_keys}, audit_queue=public_queue,
                points=[{key: point[key] for key in public_points} for point in points])


def collect(rpc, genesis, maximum_points=256, span_seconds=604800, window_seconds=7200):
    if not isinstance(genesis, str) or not IDENTITY.fullmatch(genesis):
        raise ValueError('Canonical network identity required')
    if (type(maximum_points) is not int or not 2 <= maximum_points <= 256
            or type(span_seconds) is not int or not 60 <= span_seconds <= 604800
            or type(window_seconds) is not int or not 30 <= window_seconds <= 86400):
        raise ValueError('Bounded metrics arguments required')
    snapshot = None
    for _ in range(3):
        value = validate(rpc.call('getpgnnetworkmetrics',
                                 [maximum_points, span_seconds, window_seconds]), genesis, maximum_points)
        if rpc.call('getbestblockhash') == value['bestblockhash']:
            snapshot = value
            break
    if snapshot is None:
        raise RuntimeError('Chain changed during observation; no snapshot promoted')
    encoded = json.dumps(snapshot, sort_keys=True, separators=(',', ':')).encode()
    if len(encoded) > 1024 * 1024:
        raise ValueError('Metrics snapshot exceeds its byte budget')
    return dict(schema='PGN_NETWORK_OBSERVATIONS_EXPORT_V1', genesis=genesis,
                snapshot_tip=snapshot['bestblockhash'], metrics_sha256=hashlib.sha256(encoded).hexdigest(),
                qualification=dict(
                    origin='first_on_chain_report_commitment', computation_time_authenticated=False,
                    historical_audit_status='at_snapshot_tip',
                    independent_capacity_signal='UNKNOWN',
                    reason='Report arrival is not authenticated computation time; no hardware capacity inferred',
                    registered_operators_are_online_measurement=False,
                    audits_and_cpu_shadow_excluded=True),
                native_metrics=snapshot, mining_or_consensus_changed=False)


def write_export(destination, value):
    encoded = (json.dumps(value, indent=2) + '\n').encode()
    if len(encoded) > 2 * 1024 * 1024:
        raise ValueError('Export exceeds its byte budget')
    destination = Path(destination).resolve()
    if not destination.parent.is_dir():
        raise ValueError('Select an existing output directory')
    handle, temporary = tempfile.mkstemp(prefix='.pgn-observations-', dir=destination.parent)
    try:
        with os.fdopen(handle, 'wb') as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        # Link publication is atomic and refuses an existing destination.
        os.link(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', required=True, type=Path, help='Existing local Core profile')
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--maximum-points', type=int, default=256)
    parser.add_argument('--span-seconds', type=int, default=604800)
    parser.add_argument('--window-seconds', type=int, default=7200)
    args = parser.parse_args()
    if args.profile.stat().st_size > 32768:
        raise ValueError('Core profile exceeds its byte budget')
    profile = json.loads(args.profile.read_bytes())
    rpc = CoreRpc(profile['rpc_port'], Path(profile['data_directory']) / 'testnet/.cookie',
                  profile['genesis_hash'], '')
    try:
        rpc.verify()
        result = collect(rpc, profile['genesis_hash'], args.maximum_points, args.span_seconds,
                         args.window_seconds)
    finally:
        rpc.cancel_all()
    write_export(args.output, result)
    print(json.dumps(dict(exported=str(args.output.resolve()), snapshot_tip=result['snapshot_tip'],
                          capacity_signal=result['qualification']['independent_capacity_signal'])))


if __name__ == '__main__':
    main()
