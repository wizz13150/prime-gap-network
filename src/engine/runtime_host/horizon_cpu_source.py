"""Normal-cycle CPU source with durable complete records before publication.

Computation and scientific admission are separate, as for the deferred GPU
source. This adapter never promotes a PRP computation by setting an enable flag.
"""
from pathlib import Path
import copy,hashlib,json,math,threading,time
import datetime as dt
from pgn_gap_projection import project,presentation,checked_pairs
from pgn_native_contract import fields
from pgn_session import atomic_bytes,SessionControllerLock
from horizon_engine_host import ticket_configuration

class CpuScientificSourcePending(RuntimeError):
    pass

def completion_poll_delay(status):
    """Observe the final half-second closely, without high-rate polling all day."""
    if status.get('result_resident_policy')is not True or status.get('compute_started')is not True:return .2
    try:
        left=int(status['total_positions'])-int(status['completed_positions']);rate=status['positions_per_second']
        if type(rate)not in(int,float)or not math.isfinite(rate)or rate<=0:return .2
        return .02 if 0<left<=rate*.5 else .2
    except (KeyError,ValueError,TypeError):return .2

class CpuOnlyNormalSource:
    scientific_raw_report_provisional=True
    def __init__(self,root=None,emit=lambda value:None,*,finalize=None,quantum=20000000000,stopping=None,running=None,observer=lambda value:None):
        if type(quantum)is not int or not 100<=quantum<=20000000000 or quantum%100:
            raise ValueError('CPU source quantum must be a bounded multiple of100')
        self.root=Path(root)if root is not None else None
        self.emit=emit;self.observer=observer;self.finalizer=finalize;self.quantum=quantum;self.stopping=stopping or threading.Event();self.running=running
        self.clients={};self.options={};self.grant=None;self.lock=threading.RLock();self.current_frame=None
        self.owner=None
        if self.root is not None:
            from horizon_preparation_queue import _directory
            self.root=_directory(self.root)
            self.owner=SessionControllerLock(_directory(self.root/'cpu-source-owner'))

    def require_report_finalization(self):
        return True

    def configure_cpu(self,device,inventory,enabled,threads,affinity):
        from horizon_cpu_bridge import GuiCpuClient
        host=inventory.get('host','')
        if device!='CPU/'+host.upper()+'/'+str(inventory.get('processor_group',0))or not host:
            raise ValueError('CPU endpoint ownership differs from inventory')
        if type(enabled)is not bool or type(threads)is not int or not 1<=threads<=256 or type(affinity)is not list or len(set(affinity))!=threads:
            raise ValueError('Explicit CPU enable/thread/affinity budget required')
        with self.lock:
            if device not in self.clients:self.clients[device]=GuiCpuClient(device,inventory,self.emit)
            client=self.clients[device]
            self.options[device]=dict(enabled=enabled,threads=threads,affinity=list(affinity))
        return client.configure(enabled,threads,affinity)

    def accept_cpu_reply(self,device,identity,response):
        if device not in self.clients:raise ValueError('CPU reply has no owned endpoint')
        self.clients[device].accept(identity,response)

    def cancel_cpu_transport(self,reason):
        for client in tuple(self.clients.values()):client.cancel(reason)

    def bind_cycle(self,ticket,context,cycle):
        from horizon_cpu_cycle import cycle_request
        with self.lock:self.grant=cycle_request(ticket,context,cycle)

    def clear_cycle(self):
        with self.lock:self.grant=None

    @staticmethod
    def _digest(value):
        return hashlib.sha256(json.dumps(value,sort_keys=True,separators=(',',':')).encode()).hexdigest()

    def _file(self,ticket):
        if self.root is None or len(ticket)!=64 or any(c not in'0123456789abcdef'for c in ticket):
            raise ValueError('CPU source needs its own root and canonical ticket')
        folder=self.root/'tickets'/ticket
        for part in(self.root,folder.parent,folder):
            if part.exists()and(part.is_symlink()or getattr(part.lstat(),'st_file_attributes',0)&0x400):
                raise ValueError('CPU source root cannot be a reparse point')
        folder.mkdir(parents=True,exist_ok=True)
        return folder/'cpu-source.json'

    def _save(self,file,state):
        raw=(json.dumps(state,sort_keys=True,separators=(',',':'))+'\n').encode()
        if len(raw)>1048576:raise BufferError('CPU source record budget reached; native result remains unacknowledged')
        atomic_bytes(file,raw)

    def _state(self,ticket,grant):
        file=self._file(ticket['ticket_id'])
        configuration=dict(ticket_configuration(ticket),preseal_required=True)
        identity=self._digest(dict(configuration=configuration,grant=grant))
        if file.exists():
            if file.stat().st_size>1048576:raise ValueError('Oversized CPU source journal')
            state=json.loads(file.read_bytes())
            if state.get('identity')!=identity or state.get('configuration')!=configuration:
                raise ValueError('CPU source journal belongs to another normal-cycle grant')
            checked_pairs([(int(a),int(b))for a,b in state['pairs']],configuration)
            if not int(ticket['start'])<=int(state['cursor'])<=int(ticket['end']):raise ValueError('CPU source frontier escaped its grant')
        else:
            state=dict(schema='PGN_CPU_PROVISIONAL_SOURCE_V1',identity=identity,configuration=configuration,
                cursor=ticket['start'],pairs=[],active=None,pending_ack=None,candidate_tests='0')
            self._save(file,state)
        return file,state

    def _ack(self,file,state):
        ack=state.get('pending_ack')
        if ack is None:return
        client=self.clients.get(ack['device'])
        if client is None:raise CpuScientificSourcePending('Reconnect the CPU endpoint owning the saved delivery')
        status=client.call('status')['status']
        if status['request_sha256']is not None:
            if status['request_sha256']!=ack['request_sha256']:raise ValueError('CPU has another retained delivery')
            client.call(ack['action'],request_sha256=ack['request_sha256'])
        state['pending_ack']=None;self._save(file,state)

    def _present(self,ticket,state,device,status,partial=None):
        cfg=state['configuration'];pairs=[(int(a),int(b))for a,b in state['pairs']]
        if partial is not None and sum(map(int,partial['counts100']))<=100:
            pairs+= [(int(x['lower_prime']),int(x['upper_prime']))for x in partial['best']]
        view=presentation(sorted(set(pairs)),cfg)
        positions=int(state['cursor'])-int(ticket['start'])+int(status.get('completed_positions','0'))
        selected=dict(self.options.get(device,{})).get('enabled',False)
        resource=status.get('resource_rates',{})
        continuous=(resource.get('schema')=='PGN_CPU_RESOURCE_RATES_V1'and resource.get('source')=='verified_closed_native_pieces'
            and resource.get('session')==status.get('session')and status.get('enabled')is True
            and status.get('resident_workers',0)>0 and status.get('resource_dispatch_threads',0)>0
            and status.get('result_resident_policy')is True and not status.get('budget_refused')and not status.get('error')
            and type(resource.get('positions_per_second'))in(int,float)and resource['positions_per_second']>0)
        active=selected and status.get('compute_started')is True
        rates=status.get('mining_rates',{})
        total_tests=int(state['candidate_tests'])+int(rates.get('candidate_tests','0'))
        self.current_frame=dict(schema='PGN_HORIZON_MINING_VIEW_V1',source_device=device,ticket_id=ticket['ticket_id'],
            request_id=(self.grant or{}).get('work_cycle',''),title='Network CPU work',work_role='search',
            start=ticket['start'],end=ticket['end'],positions=str(positions),positions_total=str(int(ticket['end'])-int(ticket['start'])),
            current=str(int(ticket['start'])+positions),phase='Searching'if active else('Starting'if selected else'Paused'),bits=int(ticket['start']).bit_length(),
            minimum_gap=cfg['threshold_policy']['segments'][0]['count_gap'],target_merit=cfg['target_merit'],
            validation_state='CPU computation · scientific finalization and network audits pending',
            record_pairs=view['best'],scopes=dict(ticket=view),source_stale=False,source_age_ns='0',
            observed_utc=dt.datetime.now(dt.timezone.utc).isoformat(),stream_id=self._digest(str(self.root)),
            compute_epoch=status.get('session'),
            tests=str(total_tests),speed_rate=(resource['positions_per_second']if continuous else status.get('positions_per_second'))if active else None,
            compute_tests_rate=(resource.get('tests_per_second')if continuous else rates.get('tests_per_second'))if active else None,
            average_rate=None,eta_rate=(resource['positions_per_second']if continuous else status.get('positions_per_second'))if active else None)
        self.emit(dict(schema='PGN_HORIZON_CPU_NORMAL_EVENT_V1',device_id=device,type='telemetry',status=status,resource_enabled=selected))
        self.observer(dict(phase='SEARCHING'if active else('STARTING'if selected else'PAUSED_ACK'),positions=str(positions),tests=str(total_tests)))

    def run_ticket(self,ticket,maximum_piece):
        if self.root is None:raise ValueError('CPU-only normal mining requires an owned normal-cycle executor and qualified fixed24h capacity')
        if type(maximum_piece)is not int or maximum_piece<100:raise ValueError('Bounded CPU piece size required')
        with self.lock:grant=copy.deepcopy(self.grant)
        if not grant or grant['ticket_id']!=ticket['ticket_id']:raise ValueError('CPU work has no normal-cycle grant')
        a,b=int(ticket['start']),int(ticket['end'])
        if not(1<<67)<=a<b<=(1<<68):raise ValueError('CPU source supports Owned68 only')
        file,state=self._state(ticket,grant);self._ack(file,state)
        while int(state['cursor'])<b and not self.stopping.is_set():
            if self.running is not None and not self.running.is_set():return dict(state='PAUSED')
            active=state['active']
            if active is None:
                with self.lock:device=next((k for k,v in self.options.items()if v['enabled']),None)
                if device is None:self.stopping.wait(.2);return dict(state='PAUSED')
                client=self.clients[device]
                capability=client.call('status')['status'].get('complete_record_delivery')=='PGN_CPU_COMPLETE_RECORDS_V1'
                start=int(state['cursor']);end=b if capability else min(b,start+min(self.quantum,maximum_piece//100*100))
                if(end-start)%100:raise ValueError('CPU subwork requires an exact100-partition boundary')
                request=dict(grant,start=str(start),end=str(end))
                active=dict(device=device,request=request);state['active']=active;self._save(file,state)
            device=active['device'];request=active['request'];client=self.clients.get(device)
            if client is None:raise CpuScientificSourcePending('Reconnect the CPU endpoint retaining this quantum')
            expected=self._digest(request);status=client.call('status')['status']
            if status['request_sha256']is None:
                mode=dict(keep_resident=True)if status.get('resident_result_handoff')is True else{}
                status=client.call('execute',request=request,**mode)['status']
            elif status['request_sha256']!=expected:raise ValueError('CPU endpoint retains another normal-cycle quantum')
            with self.lock:choice=dict(self.options.get(device,{}))
            if not choice.get('enabled'):client.pause();return dict(state='PAUSED')
            with self.lock:
                choice=dict(self.options.get(device,{}))
                if not choice.get('enabled'):client.pause();return dict(state='PAUSED')
                if not(status.get('enabled')is True and status.get('requested_threads')==choice['threads']and status.get('effective_affinity')==choice['affinity']):
                    client.configure(True,choice['threads'],choice['affinity'])
            while not self.stopping.is_set():
                status=client.call('status')['status']
                if status.get('request_sha256')!=expected:raise ValueError('CPU frontier changed its normal-cycle quantum')
                self._present(ticket,state,device,status)
                with self.lock:enabled=self.options[device]['enabled']
                if status['result_ready']or status.get('budget_refused')or not enabled or self.running is not None and not self.running.is_set():break
                self.stopping.wait(completion_poll_delay(status))
            keep=status.get('result_resident_policy')is True and status.get('result_ready')is True
            if not keep:client.pause()
            evidence=client.call('evidence')['evidence']
            if evidence.get('request')!=request or evidence.get('request_sha256')!=expected:
                raise ValueError('CPU evidence changed its owned quantum')
            if not evidence['complete']:
                self._present(ticket,state,device,client.call('status')['status'],evidence)
                if status.get('budget_refused'):self.stopping.wait(.5)
                return dict(state='PAUSED')
            count=sum(map(int,evidence['counts100']))
            if status.get('complete_record_delivery')=='PGN_CPU_COMPLETE_RECORDS_V1':
                records=[];after='';previous=-1
                while True:
                    page=client.call('records',request_sha256=expected,after=after,limit=64)['records']
                    if(page.get('schema')!='PGN_CPU_COMPLETE_RECORDS_V1'or page.get('request_sha256')!=expected
                        or page.get('total_records')!=str(count)or type(page.get('records'))is not list or len(page['records'])>64):raise ValueError('CPU complete record page identity/count mismatch')
                    for row in page['records']:
                        lo,hi=int(row['lower_prime']),int(row['upper_prime'])
                        if not previous<lo or not int(request['start'])<=lo<int(request['end'])or hi-lo!=row['gap']:raise ValueError('CPU complete records are not ordered owned gaps')
                        previous=lo;records.append(row)
                    if len(records)>count:raise ValueError('CPU complete record delivery exceeded its count')
                    cursor=page.get('next_after')
                    if cursor is None:break
                    if not page['records']or cursor!=page['records'][-1]['lower_prime']:raise ValueError('CPU complete record cursor failed to advance')
                    after=cursor
            elif count>100:
                # No missing records are invented from a truncated top100.
                # Keep this result until the caller explicitly repartitions.
                raise BufferError('CPU quantum needs smaller subdivisions; complete result retained')
            else:records=evidence['best']
            if len(records)!=count:raise ValueError('CPU quantum lacks its complete primary records')
            additions=[(int(x['lower_prime']),int(x['upper_prime']))for x in records]
            bins=[0]*100;length=int(request['end'])-int(request['start'])
            for lo,hi in additions:bins[(lo-int(request['start']))*100//length]+=1
            if bins!=list(map(int,evidence['counts100'])):raise ValueError('CPU complete records do not conserve their exact partition counts')
            pairs=[(int(x),int(y))for x,y in state['pairs']]+additions
            checked_pairs(pairs,state['configuration'])
            state['pairs']=[[str(x),str(y)]for x,y in sorted(pairs)];state['cursor']=request['end']
            state['candidate_tests']=str(int(state['candidate_tests'])+int(evidence['candidate_tests']))
            state['active']=None;state['pending_ack']=dict(device=device,request_sha256=expected,action='ack_result')
            self._save(file,state);self._ack(file,state)
        if int(state['cursor'])!=b:return dict(state='PAUSED')
        counts,best=project([(int(x),int(y))for x,y in state['pairs']],state['configuration'])
        body=dict(schema='PGN_HORIZON_COMPUTED_REPORT_V1',ticket_id=ticket['ticket_id'],start=ticket['start'],end=ticket['end'],
            positions=str(b-a),policy_sha256=state['configuration']['threshold_policy']['sha256'],counts=counts,best=best)
        return dict(body=body,sha256=self._digest(body),network_accepted=False,scientific_finalization_pending=True)

    def finalize_report(self,work,computed):
        if self.finalizer is None:raise CpuScientificSourcePending('CPU result retained; complete scientific source is not ready for sealing')
        return self.finalizer(work,computed)

    def close(self):
        self.stopping.set()
        try:
            for client in tuple(self.clients.values()):client.pause()
        finally:
            if self.owner is not None:self.owner.release();self.owner=None
