"""Closed routing fixture; never performs CPU mining or network I/O."""
from pathlib import Path
import argparse,json,sys
p=argparse.ArgumentParser();p.add_argument('--echo',action='store_true');p.add_argument('--host');p.add_argument('--workspace',type=Path);p.add_argument('--initial-threads',type=int,default=1);p.add_argument('--network',action='store_true');p.add_argument('--cpu-only',action='store_true');a=p.parse_args()
if a.network:
    assert a.cpu_only
    pending={};generation=0
    for line in sys.stdin:
        c=json.loads(line)
        if c['action']=='quit':break
        if c['action']=='cpu_configure':
            generation+=1;pending[c['id']]=c
            print(json.dumps(dict(schema='PGN_HORIZON_CPU_COMMAND_V1',device_id=c['device_id'],bridge_id=c['id'],
                command=dict(action='configure',enabled=c['enabled'],threads=c['threads'],affinity=c['affinity'],generation=generation))),flush=True)
        elif c['action']=='cpu_reply':
            old=pending.pop(c['bridge_id']);r=c['response']
            print(json.dumps(dict(schema='PGN_HORIZON_CPU_NORMAL_EVENT_V1',device_id=c['device_id'],type='control',
                id=old['id'],applied=r['ok'],status=r['status'],resource_enabled=old['enabled'])),flush=True)
    raise SystemExit
if a.echo:
    for line in sys.stdin:print(line.strip(),flush=True)
    raise SystemExit
a.workspace.mkdir(parents=True,exist_ok=True)
inventory=dict(host=a.host,processor_group=0,maximum_workers=4,available_cpus=list(range(6)),topology=[dict(cpus=[0,1]),dict(cpus=[2,3]),dict(cpus=[4,5])],compute_started=False,control_version=2,normal_cycle_executor=True,standalone_audit_allocation=False)
status=dict(state='IDLE',enabled=False,generation=0,requested_threads=a.initial_threads,threads_active=0,resident_workers=0,worker_pids=[],network_accepted=False,compute_started=False,fixture_workspace=str(a.workspace),fixture_host=a.host)
print(json.dumps(dict(event='ready',schema='PGN_CPU_JSONLINES_V1',compute_started=False)),flush=True)
for line in sys.stdin:
    command=json.loads(line);action=command['action'];reply=dict(id=command['id'],ok=True)
    if action=='inventory':reply['inventory']=inventory
    elif action=='configure':status.update(enabled=command['enabled'],requested_threads=command['threads'],generation=command['generation'],state='WAITING_NORMAL_CYCLE'if command['enabled']else'OFF')
    elif action=='pause':status.update(enabled=False,state='PAUSED',generation=command['generation'])
    elif action=='execute':
        assert command['request']['schema']=='PGN_CPU_CYCLE_WORK_V1'
        (a.workspace/'owned-request.json').write_text(json.dumps(command['request']))
    elif action=='close':status.update(state='CLOSED',enabled=False)
    elif action!='status':reply.update(ok=False,error=dict(message='Unsupported closed fixture action'))
    reply['status']=status;print(json.dumps(reply),flush=True)
    if action=='close':break
