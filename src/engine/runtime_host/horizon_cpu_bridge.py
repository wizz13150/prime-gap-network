"""GUI-owned local/remote CPU executors; identities never inherit GPU host paths."""
import threading
import queue
from horizon_cpu_cycle import MixedNormalSource

class GuiCpuClient:
    def __init__(self,device,inventory,emit,timeout=60):
        self.device=device;self.inventory=inventory;self.emit=emit;self.timeout=timeout
        self.lock=threading.RLock();self.pending={};self.sequence=0;self.generation=0;self.failure=None
    def call(self,action,**fields):
        if action=='inventory':return dict(inventory=self.inventory)
        with self.lock:
            if self.failure:raise OSError(self.failure)
            self.sequence+=1;identity=str(self.sequence);answer=queue.Queue(maxsize=1)
            self.pending[identity]=answer
            try:
                written=self.emit(dict(schema='PGN_HORIZON_CPU_COMMAND_V1',device_id=self.device,bridge_id=identity,
                    command=dict(action=action,**fields)))
                if written is False:raise OSError('Normal miner control output disconnected')
                try:value=answer.get(timeout=self.timeout)
                except queue.Empty:raise TimeoutError('GUI-owned CPU boundary reply timed out for '+self.device)
                if value.get('ok')is not True:raise ValueError(value.get('error',{}).get('message','CPU executor refused work'))
                return value
            finally:self.pending.pop(identity,None)
    def accept(self,identity,response):
        if type(identity)is not str or not isinstance(response,dict):raise ValueError('Exact CPU bridge identity/response object required')
        pending=self.pending.get(identity)
        if pending is None:raise ValueError('CPU bridge response has no matching owned command')
        try:pending.put_nowait(response)
        except queue.Full:raise ValueError('Duplicate CPU bridge response')
    def configure(self,enabled,threads,affinity):
        self.generation+=1
        return self.call('configure',enabled=enabled,threads=threads,affinity=affinity,generation=self.generation)
    def pause(self):
        self.generation+=1;return self.call('pause',generation=self.generation)
    def close(self):self.pause()
    def cancel(self,reason):
        self.failure=reason
        for pending in tuple(self.pending.values()):
            try:pending.put_nowait(dict(ok=False,error=dict(message=reason)))
            except queue.Full:pass

class LogicalMinerCpuResources:
    """One normal cycle may use CPU resources on several distinct hosts."""
    def __init__(self,gpu,root,emit):
        self.gpu=gpu;self.root=root;self.emit=emit;self.lanes={};self.head=gpu;self.binding=None
        self.control_lock=threading.RLock()
    def __getattr__(self,name):return getattr(self.gpu,name)
    def configure_cpu(self,device,inventory,enabled,threads,affinity):
        host=inventory.get('host','');expected='CPU/'+host.upper()+'/'+str(inventory.get('processor_group',0))
        if device!=expected or not host:raise ValueError('CPU device identity differs from its endpoint host')
        with self.control_lock:
            if device not in self.lanes:
                client=GuiCpuClient(device,inventory,self.emit)
                def observe(event):
                    self.emit(dict(schema='PGN_HORIZON_CPU_NORMAL_EVENT_V1',device_id=device,**event))
                lane=MixedNormalSource(self.head,client,self.root/'cpu-resources'/device.replace('/','_'),threads=threads,affinity=affinity,observer=observe,
                    resource_identity=dict(device_id=device,host=host,processor_group=inventory.get('processor_group',0)))
                if self.binding:lane.bind_cycle(*self.binding)
                self.lanes[device]=lane;self.head=lane
            lane=self.lanes[device]
        return lane.configure_cpu(enabled,threads,affinity)
    def accept_cpu_reply(self,device,identity,response):
        if type(device)is not str:raise ValueError('Canonical CPU device identity required')
        if device not in self.lanes:raise ValueError('Reply belongs to an unknown CPU resource')
        self.lanes[device].cpu.accept(identity,response)
    def bind_cycle(self,*binding):
        with self.control_lock:
            self.binding=binding
            for lane in self.lanes.values():lane.bind_cycle(*binding)
    def clear_cycle(self):
        with self.control_lock:
            self.binding=None
            for lane in self.lanes.values():lane.clear_cycle()
    def run_ticket(self,*args):
        with self.control_lock:head=self.head
        return head.run_ticket(*args)
    def run_scheduled_ticket(self,ticket,maximum_piece,should_yield):
        with self.control_lock:
            head=self.head
            mixed=any(lane.enabled for lane in self.lanes.values())
        # Existing CPU qualification owns a whole result boundary. Preserve
        # that contract until mixed-resource handoffs are independently tested.
        if mixed:return head.run_ticket(ticket,maximum_piece)
        return self.gpu.run_scheduled_ticket(ticket,maximum_piece,should_yield)
    def close(self):self.head.close()
    def cancel_cpu_transport(self,reason):
        for lane in tuple(self.lanes.values()):lane.cpu.cancel(reason)
