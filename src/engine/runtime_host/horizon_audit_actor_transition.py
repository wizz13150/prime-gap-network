"""Retain pilot answer publishers while real service belongs to the miner."""
from horizon_core_rpc import CoreRpcError


def native_service_actors(worker, lab_actors):
    if lab_actors is None:
        return False
    try:
        status = worker.rpc.call('getpgnworkdutystatus', [worker.address()])
    except CoreRpcError as error:
        if error.code == -32601:
            return False
        raise
    if (not isinstance(status, dict)
            or status.get('schema') != 'PGN_NATIVE_WORK_DUTY_STATUS_V1'
            or status.get('genesis') != worker.args.genesis
            or type(status.get('enforced')) is not bool):
        raise ValueError('Actor transition requires the owning Core service policy')
    return status['enforced']
