"""UI-agnostic resident shared68 service. NDJSON control; no legacy miner imports."""
import argparse,ctypes,hashlib,json,os,pathlib,queue,sys,threading,time,uuid
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parent))
from contracts import SOURCE,COVERAGE,digest,integer,validate
from merit_policy import next_piece
import cycle_work
from native_transport import HASHES,Job,Load,Worker,inventory,verify
from runtime_metrics import CpuMetrics,PositionRateWindow,MiningCounterWindow
SCHEMA='PGN_CPU_JSONLINES_V1';CONTROL_VERSION=2
LOAD_LIMIT_PERCENT=75
RESUME_HEADROOM_PERCENT=0
RESUME_STABLE_SECONDS=2
POLICY_SHA='96e656691a2f7e3f8594a1cfdc59c04a94dd3c86a2d9aadadb1f70591bb0639a'

class ControlLease:
 # Existing inventory/status requests renew ownership; no extra heartbeat or
 # work is introduced. A lost pipe must not retain the workspace indefinitely.
 def __init__(self,timeout=45.,drain_grace=5.,clock=None):
  if timeout<=0 or drain_grace<0:raise ValueError('Positive control lease required')
  self.clock=clock or time.monotonic;self.timeout=timeout;self.drain_grace=drain_grace;self.last=self.clock();self.closing_since=None
 def observe(self):
  if self.closing_since is None:self.last=self.clock()
 def close_due(self,eof=False):
  now=self.clock()
  if self.closing_since is None and(eof or now-self.last>=self.timeout):self.closing_since=now
  return self.closing_since is not None
 def drain_expired(self):
  return self.closing_since is not None and self.clock()-self.closing_since>=self.drain_grace
def replace_checkpoint(source,target):
 # A Windows reader may briefly deny replacement of the destination. Retain
 # the fsynced candidate and retry only sharing/access conflicts, with a bound.
 # Neither a partial write nor a different checkpoint is accepted on failure.
 deadline=time.monotonic()+2.
 while True:
  try:os.replace(source,target);return
  except PermissionError as e:
   if getattr(e,'winerror',None)not in(5,32,33)or time.monotonic()>=deadline:raise
   time.sleep(.02)
def emit(value):
 data=json.dumps(value,separators=(',',':'))
 if len(data.encode())>32768:raise ValueError('Response exceeds32KiB')
 print(data,flush=True)
def validate_reply(r,task,cpu,simd):
 ordinal,a,b,part,seq,gap=task
 if r.get('type')!='result' or r.get('state')!='COMPLETE' or r.get('backend')!='cpu_shared_gpu_lineage68' or r.get('job_id')!=seq or r.get('start')!=str(a) or r.get('end')!=str(b) or integer(r.get('positions'))!=b-a or r.get('minimum_gap')!=gap or r.get('pinned_cpu')!=cpu or r.get('affinity_verified')is not True or r.get('canonical_source_manifest_sha256')!=SOURCE or r.get('coverage_contract')!=COVERAGE or r.get('production_admitted')is not False or r.get('implementation')!=('shared_avx2_8owner'if simd else 'shared_scalar64') or r.get('sieve_layout')!='canonical_tiled':raise ValueError('Native completion does not match closed owned piece')
 if r.get('schema')!='PGN_CPU_SHARED_OWNED_PIECE_V2' or r.get('audit_only')is not False:raise ValueError('Owned executor V2 records required')
 expected=dict(start=str(a),end=str(b),gap_owner='lower_prime',right_crossing_verified=True)
 if r.get('owned_frontier')!=expected or r.get('records_verification')!=cycle_work.RECORDS:raise ValueError('Owned boundary/record evidence required')
 c=integer(r.get('count'))
 if not 0<=c<=b-a:raise ValueError('Invalid bounded native count')
 seconds=r.get('seconds')
 if type(seconds)not in(int,float)or not 0<=seconds<3600:raise ValueError('Invalid native duration')
 cycle_work.records(r.get('best'),a,b,c,gap)
 if not 0<=integer(r.get('candidate_tests'))<=b-a:raise ValueError('Invalid native candidate counter')
 return c

def validate_memory_pages(v):
 if not isinstance(v,dict)or v.get('schema')!='PGN_CPU_WORKER_MEMORY_PAGES_V1':raise ValueError('Native memory-page schema required')
 for key in('allocator_supported','verified','compute_buffer_used'):
  if type(v.get(key))!=bool:raise ValueError('Native page evidence boolean required')
 allocated=integer(v.get('allocated_bytes'));bitmap=integer(v.get('bitmap_bytes'));large=integer(v.get('large_page_bytes_in_use'))
 if not 0<=bitmap<=allocated<=67108864 or not 0<=large<=allocated:raise ValueError('Invalid native bitmap/page extent')
 if large and not(v['allocator_supported']and v['verified']and v['compute_buffer_used']):raise ValueError('Unverified native large-page claim refused')
 if type(v.get('last_error'))!=int:raise ValueError('Native page error integer required')
 return dict(v)

class Service:
 def __init__(self,args):
  self.args=args;self.workspace=args.workspace.resolve();self.workspace.mkdir(parents=True,exist_ok=True);self.lock=open(self.workspace/'shared68.lock','a+b');self.lock.seek(0,2)
  if self.lock.tell()==0:self.lock.write(b'0');self.lock.flush()
  self.lock.seek(0)
  if os.name=='nt':
   import msvcrt;msvcrt.locking(self.lock.fileno(),msvcrt.LK_NBLCK,1)
  else:
   import fcntl;fcntl.flock(self.lock.fileno(),fcntl.LOCK_EX|fcntl.LOCK_NB)
  self.cp=self.workspace/'shared68-checkpoint.json';verify(args.engine,HASHES[os.name][0]);self.inv=inventory(args.inventory);self.events=queue.Queue(maxsize=64);self.commands=queue.Queue(maxsize=32);self.job=Job();self.load=Load();self.workers={};self.pending=None;self.generation=0;self.enabled=False;self.threads=1;self.affinity=[];self.state='IDLE';self.failure=None;self.budget_refused=False;self.request=None;self.parts=[];self.probe=False;self.total=0;self.committed=0;self.next_offset=0;self.counts=[];self.completed={};self.seq=0;self.result=None;self.compute_seconds=0.;self.run_start=None;self.running_seconds=0.;self.session=uuid.uuid4().hex;self.closed=False;self.max_buffered=0;self.completed_piece_count=0;self.rate_base=0;self.best=[];self.tests=0
  self.history={};self.budget_resume_since=None;self.budget_retry_after=0.;self.eof_requested=False;self.maximum_machine_load=0.;self.coordinator_cpu=None;self.own_cpu_start=sum(os.times()[:2])
  self.simd=bool(self.inv.get('features',{}).get('avx2_usable')) and not args.scalar
  self.threads=min(args.initial_threads,self.inv["maximum_workers"]);self.affinity=self.preferred(self.threads)
  self.restore()
  if os.name!='nt':self.pin_coordinator()
  self.metrics=CpuMetrics();self.rate_window=PositionRateWindow();self.science_rates=MiningCounterWindow()
 def preferred(self,n):
  out=[]
  for p in(0,1):
   for group in self.inv['topology']:
    for i,cpu in enumerate(group['cpus']):
     if (i==0)==(p==0)and cpu in self.inv['available_cpus']and cpu not in out:out.append(cpu)
  return out[:n]
 def identity(self):return dict(control_version=CONTROL_VERSION,worker_sha256=HASHES[os.name][0],source_manifest=SOURCE,policy_sha256=POLICY_SHA)
 def persist(self):
  value=dict(schema='PGN_SHARED68_ORDERED_CHECKPOINT_V1',identity=self.identity(),request=self.request,probe=self.probe,committed=str(self.committed),counts=[str(c)for c in self.counts],compute_seconds=self.compute_seconds,result=self.result,completed_piece_count=self.completed_piece_count,best=self.best,candidate_tests=str(self.tests))
  value['sha256']=digest(value);data=json.dumps(value,separators=(',',':')).encode()
  if len(data)>32768:raise ValueError('Checkpoint exceeds32KiB')
  tmp=self.cp.with_suffix('.pending')
  with open(tmp,'wb')as f:f.write(data);f.flush();os.fsync(f.fileno())
  replace_checkpoint(tmp,self.cp)
 def restore(self):
  if not self.cp.exists():return
  if self.cp.stat().st_size>32768:raise ValueError('Checkpoint exceeds32KiB')
  v=json.loads(self.cp.read_bytes());signature=v.pop('sha256',None)
  if digest(v)!=signature or v['identity']!=self.identity():raise ValueError('Checkpoint identity/integrity mismatch')
  if v['request']is None:return
  self.request=v['request'];self.probe=v['probe'];self.parts=validate(self.request,True)if self.probe else cycle_work.partitions(self.request);self.total=sum(integer(p['end'])-integer(p['start'])for p in self.parts);self.committed=integer(v['committed']);self.counts=[integer(c)for c in v['counts']]
  if not 0<=self.committed<=self.total or len(self.counts)!=len(self.parts)or any(c<0 for c in self.counts)or sum(self.counts)>self.committed:raise ValueError('Checkpoint frontier invalid')
  self.best=cycle_work.records(v.get('best'),integer(self.parts[0]['start']),integer(self.parts[-1]['end']),sum(self.counts),0);self.tests=integer(v.get('candidate_tests'))
  frontiers=cycle_work.frontier(self.parts,self.committed)
  if self.tests<0 or any(c>integer(row['covered_end'])-integer(row['start'])for c,row in zip(self.counts,frontiers))or any(not any(integer(row['start'])<=integer(g['lower_prime'])<integer(row['covered_end'])for row in frontiers)for g in self.best):raise ValueError('Checkpoint counters/best exceed durable owned frontier')
  self.next_offset=self.committed;self.rate_base=self.committed;self.compute_seconds=float(v['compute_seconds']);self.result=v['result'];self.completed_piece_count=integer(v.get('completed_piece_count',0))
  if self.result is not None and(self.committed!=self.total or self.result['request_sha256']!=digest(self.request)or self.result['counts']!=[str(c)for c in self.counts]):raise ValueError('Result conflicts with durable coverage')
  if self.result is None and self.committed==self.total:self.result=self.computed_result();self.persist()
  self.state='PROBE_COMPLETE'if self.result and self.probe else 'CPU_WORK_SOURCE_PROOF_PENDING'if self.result else 'PAUSED_RECOVERED'
 def computed_result(self):
  if self.committed!=self.total or(not self.probe and len(self.parts)!=100):raise ValueError('Normal CPU completion requires exact full assigned coverage')
  return dict(schema='PGN_CPU_SHARED68_PROBE_RESULT_V1'if self.probe else 'PGN_CPU_CYCLE_COMPUTATION_V1',request_sha256=digest(self.request),state='PROBE_COMPLETE'if self.probe else 'CPU_WORK_SOURCE_PROOF_PENDING',counts=[str(c)for c in self.counts],completed_positions=str(self.committed),full_audit=False,coverage_contract=COVERAGE,production_admitted=False,network_accepted=False)
 def pin_coordinator(self):
  available=[c for c in self.inv['available_cpus']if c not in self.affinity]
  if not available:raise ValueError('No interactive reserve CPU')
  cpu=max(available)
  if os.name=='nt':
   k=ctypes.WinDLL('kernel32');k.GetCurrentProcess.restype=ctypes.c_void_p;k.SetProcessAffinityMask.argtypes=[ctypes.c_void_p,ctypes.c_size_t]
   if not k.SetProcessAffinityMask(k.GetCurrentProcess(),1<<cpu):raise OSError('Coordinator affinity failed')
  else:
   os.sched_setaffinity(0,{cpu})
   for tid in pathlib.Path('/proc/self/task').iterdir():
    try:os.sched_setaffinity(int(tid.name),{cpu})
    except ProcessLookupError:pass
  self.coordinator_cpu=cpu
 def estimated_worker_load(self):
  # GetSystemTimes reports machine load, not utilization within our affinity
  # subset. Keep eligibility/reserve based on available_cpus, but use the same
  # system denominator for predicted added load. Retain conservative behavior
  # for old inventories and unqualified multi-group machines.
  count=self.inv.get('logical_cpus_system');available=len(self.inv['available_cpus'])
  if type(count)is not int or not available<=count<=64:count=available
  return self.threads*100/count
 def admit_budget(self):
  # All compute pieces are drained here. Measure a fresh idle/background window.
  self.load.reset();time.sleep(.26);value=self.load.update()
  if value is None or value+self.estimated_worker_load()>LOAD_LIMIT_PERCENT:return False
  return True
 def recover_budget(self):
  # Only an explicitly enabled, retained assignment can resume. All workers
  # have drained before this background-only measurement is considered.
  if not self.enabled or self.pending or not self.request or self.result:return
  now=time.monotonic();value=self.load.update();capacity=LOAD_LIMIT_PERCENT-RESUME_HEADROOM_PERCENT-self.estimated_worker_load()
  if value is None or value>capacity:self.budget_resume_since=None;return
  if self.budget_resume_since is None:self.budget_resume_since=now;return
  if now-self.budget_resume_since<RESUME_STABLE_SECONDS or now<self.budget_retry_after:return
  self.budget_retry_after=now+RESUME_STABLE_SECONDS;self.budget_resume_since=None
  if not self.admit_budget():return
  self.budget_refused=False;self.start_workers();self.state='RUNNING'
 def remember_pages(self,w,v):
  w.memory_pages=validate_memory_pages(v);w.memory_sample_time=time.monotonic();w.memory_query_pending=False
 def memory_pages_status(self):
  now=time.monotonic();large_workers=large_bytes=0;last_error=0;stale=0;supported=0;used_workers=0;missing_right=False;allocation_refused=False
  for w in self.workers.values():
   v=w.memory_pages;supported+=bool(v.get('allocator_supported'))
   if now-w.memory_sample_time>2:stale+=1;continue
   used_workers+=bool(v.get('compute_buffer_used'))
   error=v.get('last_error',0);last_error=last_error or error
   if v.get('privilege_error')==1300 or(error==1314 and v.get('privilege_present')is False):missing_right=True
   allocation_refused=allocation_refused or bool(error)
   used=integer(v.get('large_page_bytes_in_use','0'))
   if used and v.get('verified')and v.get('compute_buffer_used'):large_workers+=1;large_bytes+=used
  n=len(self.workers);mode=getattr(getattr(self,'args',None),'large_pages','off');active=bool(n)and large_workers==n and large_bytes>0
  if not n:cause='no_task'if getattr(self,'request',None)is None else'off_or_paused'
  elif stale:cause='stale_measurement'
  elif active:cause='active_verified'
  elif mode=='off':cause='disabled'
  elif supported<n:cause='allocator_unsupported'
  elif missing_right:cause='missing_lock_memory_privilege'
  elif any(v.memory_pages.get('compute_buffer_used')and not v.memory_pages.get('verified')for v in self.workers.values()):cause='verification_unavailable'
  elif allocation_refused:cause='allocation_refused'
  elif not used_workers:cause='awaiting_bitmap_use'
  elif large_workers:cause='partial_verified'
  else:cause='ordinary_pages_verified'
  return dict(schema='PGN_CPU_MEMORY_PAGES_V1',allocator_supported=not n or supported==n,large_page_workers=large_workers,large_page_bytes_in_use=str(large_bytes),resident_workers=n,all_resident_workers_large_pages=active,last_error=last_error,stale_workers=stale,cause=cause,requested_mode=mode)
 def request_memory_samples(self):
  if self.pending or self.state!='RUNNING':return
  now=time.monotonic()
  for w in self.workers.values():
   if w.ready and not w.task and not w.memory_query_pending and now-w.memory_sample_time>=.5:w.memory_query_pending=True;w.send('MEMORY')

 def status(self):
  self.request_memory_samples()
  active=sum(w.task is not None for w in self.workers.values());elapsed=self.running_seconds+(time.monotonic()-self.run_start if self.run_start else 0);rate=(self.committed-self.rate_base)/elapsed if elapsed>0 else 0
  computing=self.enabled and self.state=='RUNNING'and bool(self.workers)and not self.budget_refused and self.result is None
  measured_rate=self.rate_window.observe(self.committed,computing)
  science_rates=self.science_rates.observe(digest(self.request)if self.request else None,elapsed,self.committed,self.tests,sum(self.counts),computing)
  metrics=self.metrics.sample(list(self.workers.values()),self.threads)
  return dict(mining_rates=science_rates,memory_pages=self.memory_pages_status(),cpu_telemetry=metrics,session_rate_positions_per_second=rate,state=self.state,enabled=self.enabled,generation=self.generation,requested_threads=self.threads,threads_active=active,active_workers=active,resident_workers=len(self.workers),effective_affinity=[c for c,w in self.workers.items()if w.ready],coordinator_cpu=self.coordinator_cpu,coordinator_cpu_seconds=sum(os.times()[:2])-self.own_cpu_start,worker_statistics=[dict(cpu=c,**v)for c,v in self.history.items()]+[dict(cpu=c,positions=str(w.positions),native_seconds=w.native_seconds,pid=w.p.pid,resident=True)for c,w in self.workers.items()],worker_pids=[dict(pid=w.p.pid,cpu=c,busy=w.task is not None)for c,w in self.workers.items()],completed_positions=str(self.committed),durable_completed_positions=str(self.committed),total_positions=str(self.total),inflight_pieces=active,buffered_closed_pieces=len(self.completed),maximum_buffered_closed_pieces=self.max_buffered,completed_piece_count=self.completed_piece_count,rate_positions_per_second=measured_rate,positions_per_second=measured_rate,compute_seconds=self.compute_seconds,wall_active_seconds=elapsed,machine_load_percent=self.load.value,maximum_machine_load_percent=self.maximum_machine_load,total_load_limit_percent=75,budget_refused=self.budget_refused,error=self.failure,result_ready=self.result is not None,probe=self.probe,request_sha256=digest(self.request)if self.request else None,coverage_contract=COVERAGE,production_admitted=False,network_accepted=False,compute_started=computing,session=self.session)
 def reply(self,c,ok=True,error=None,**extra):
  v=dict(id=c.get('id'),ok=ok,status=self.status(),**extra)
  if error:v['error']=dict(code='REFUSED',message=str(error)[:300])
  if len(json.dumps(v,separators=(',',':')).encode())>32768 and('evidence'in extra or'result'in extra):
   # Preserve all owned counts/best/frontiers. Full telemetry has its separate
   # status command and must not inflate a bounded computation receipt.
   v['status']={k:x for k,x in v['status'].items()if k not in('worker_statistics','worker_pids','effective_affinity','cpu_telemetry','memory_pages')}
   v['status']['details']='Full telemetry available through status command'
  emit(v)
 def load_inventory(self):
  v=dict(self.inv);v.update(compute_started=False,owned_bits=[68],planned_bits=[67,95],worker_supported=True,production_admitted=False,normal_total_load_limit_percent=75,allocator_supported=True,memory_pages_schema='PGN_CPU_MEMORY_PAGES_V1',control_version=CONTROL_VERSION,normal_cycle_executor=True,standalone_audit_allocation=False,owned_work_schema=cycle_work.SCHEMA)
  return v
 def evidence(self):
  return dict(schema='PGN_CPU_CYCLE_EVIDENCE_V1',request=self.request,request_sha256=digest(self.request)if self.request else None,executor=dict(host=self.inv['host'],processor_group=self.inv.get('processor_group',0)),counts100=[str(c)for c in self.counts],best=self.best,frontiers=cycle_work.frontier(self.parts,self.committed),completed_positions=str(self.committed),candidate_tests=str(self.tests),complete=self.committed==self.total and self.request is not None,identity=self.identity(),coverage_contract=COVERAGE,production_admitted=False,network_accepted=False)
 def native_event(self,w,r):
  if w not in self.workers.values():return
  if r is None:
   if not w.stopped:raise ValueError('Resident worker exited before acknowledged STOP')
   return
  if r.get('type')=='memory_pages':
   if not w.memory_query_pending:raise ValueError('Unrequested worker memory response')
   self.remember_pages(w,r.get('memory_pages'));return
  if r.get('type')=='ready':
   if w.ready or r.get('pinned_cpu')!=w.cpu or r.get('affinity_verified')is not True:raise ValueError('Invalid worker admission')
   self.remember_pages(w,r.get('memory_pages'));w.ready=True;return
  if r.get('type')=='stopped':w.stopped=True;return
  if not w.task:raise ValueError('Unexpected native result without owned task')
  count=validate_reply(r,w.task,w.cpu,self.simd);self.remember_pages(w,r.get('memory_pages'));ordinal,a,b,part,seq,gap=w.task
  if ordinal in self.completed:raise ValueError('Duplicate closed piece')
  self.completed[ordinal]=(b-a,part,count,float(r['seconds']),r['best'],integer(r['candidate_tests']));w.positions+=b-a;w.native_seconds+=float(r['seconds']);w.task=None;self.max_buffered=max(self.max_buffered,len(self.completed));changed=False
  while self.committed in self.completed:
   length,k,c,t,best,tests=self.completed.pop(self.committed);self.best=cycle_work.merge_best(self.best,best);self.tests+=tests;self.counts[k]+=c;self.committed+=length;self.compute_seconds+=t;self.completed_piece_count+=1;changed=True
  if changed:
   if self.committed==self.total:
    self.result=self.computed_result()
    self.state=self.result['state'];self.enabled=False
   self.persist()
 def pump(self,timeout=.01):
  try:w,r=self.events.get(timeout=timeout);self.native_event(w,r)
  except queue.Empty:pass
 def stop_workers(self,keep=()):
  removed=[w for c,w in self.workers.items()if c not in keep]
  for w in removed:
   if w.task:raise ValueError('Cannot acknowledge stop before piece boundary')
   w.send('STOP')
  deadline=time.monotonic()+5
  while any(not w.stopped for w in removed):
   if time.monotonic()>deadline:raise ValueError('Worker STOP acknowledgement timed out')
   self.pump()
  for w in removed:
   w.p.wait(timeout=2)
   if w.p.returncode:raise ValueError('Worker failed while draining')
   old=self.history.get(w.cpu,dict(positions='0',native_seconds=0.))
   self.history[w.cpu]=dict(positions=str(int(old['positions'])+w.positions),native_seconds=old['native_seconds']+w.native_seconds,last_pid=w.p.pid,resident=False)
   del self.workers[w.cpu]
 def start_workers(self):
  self.pin_coordinator()
  for cpu in self.affinity:
   if cpu not in self.workers:self.workers[cpu]=Worker(self.args.engine,cpu,self.simd,self.events,self.job,self.args.large_pages)
  deadline=time.monotonic()+5
  while any(not w.ready for w in self.workers.values()):
   if time.monotonic()>deadline:raise ValueError('Worker readiness timed out')
   self.pump()
 def new_work(self,c,probe):
  if self.request is not None:raise ValueError('Existing assignment/result retained; explicit matching discard orack_result required')
  request=c.get('request');parts=cycle_work.partitions(request)if not probe else validate(request,True)
  self.request=request;self.parts=parts;self.probe=probe;self.total=sum(integer(p['end'])-integer(p['start'])for p in parts);self.committed=0;self.next_offset=0;self.counts=[0]*len(parts);self.completed={};self.result=None;self.compute_seconds=0.;self.completed_piece_count=0;self.running_seconds=0.;self.run_start=None;self.state='PAUSED';self.rate_base=0;self.budget_refused=False;self.failure=None;self.best=[];self.tests=0;self.persist()
 def locate(self,offset):
  base=0
  for k,p in enumerate(self.parts):
   a,b=integer(p['start']),integer(p['end']);size=b-a
   if offset<base+size:return k,a+offset-base,b
   base+=size
  raise ValueError('Logical offset outside assigned partitions')
 def dispatch(self):
  value=self.load.update()
  if value is not None and any(w.task for w in self.workers.values()):self.maximum_machine_load=max(self.maximum_machine_load,value)
  if not self.enabled or not self.request or self.result or self.pending or self.budget_refused:return
  if self.load.update()is not None and self.load.value>LOAD_LIMIT_PERCENT:
   self.budget_refused=True;self.state='DRAINING_BUDGET';return
  for w in self.workers.values():
   if w.task or not w.ready:continue
   outstanding=len(self.completed)+sum(x.task is not None for x in self.workers.values())
   if outstanding>=max(1,2*self.threads)or self.next_offset>=self.total:continue
   k,a,b=self.locate(self.next_offset);piece=next_piece(a,b,self.args.piece_positions)
   if piece.bits!=68:raise ValueError('Unported owned width')
   self.seq+=1;w.task=(self.next_offset,a,piece.end,k,self.seq,piece.minimum_gap);self.next_offset+=piece.end-a
   w.send(f'TASK {self.seq} {a>>64} {a&((1<<64)-1)} {piece.end-a} {piece.minimum_gap}')
  if any(w.task for w in self.workers.values()):
   self.state='RUNNING'
   if self.run_start is None:self.run_start=time.monotonic()
 def finish_pending(self):
  if any(w.task for w in self.workers.values()):return
  if self.run_start is not None:self.running_seconds+=time.monotonic()-self.run_start;self.run_start=None
  if self.result and self.pending is None:self.stop_workers();self.budget_refused=False;self.state=self.result['state'];return
  if self.budget_refused and self.pending is None:self.stop_workers();self.state='BUDGET_PAUSED';self.recover_budget();return
  if self.pending is None:return
  c=self.pending;self.pending=None;action=c['action']
  if action in('configure','pause'):
   enabled=c.get('enabled',False)if action=='configure'else False
   if action=='configure':self.threads=c['threads'];self.affinity=c['affinity']
   self.generation=c['generation'];self.enabled=enabled;self.budget_refused=False;self.budget_resume_since=None
   if not enabled:self.stop_workers();self.state='PAUSED'if self.request else 'OFF'
   elif self.request and self.result is None:
    self.stop_workers(keep=self.affinity)
    if not self.admit_budget():self.stop_workers();self.budget_refused=True;self.state='BUDGET_PAUSED';self.reply(c);return
    self.start_workers();self.state='RUNNING'
   else:self.stop_workers();self.state='WAITING_NORMAL_CYCLE'if self.result is None else self.result['state']
   self.persist();self.reply(c);return
  if action=='close':self.enabled=False;self.stop_workers();self.persist();self.state='CLOSED';self.reply(c);self.closed=True;return
  raise ValueError('Unknown pending mutation')
 def command(self,c):
  if not isinstance(c,dict)or type(c.get('id'))not in(int,str)or 'action'not in c:raise ValueError('id/action object required')
  a=c['action']
  allowed={'inventory':set(),'status':set(),'execute':{'request'},'probe':{'request'},'evidence':set(),'configure':{'enabled','threads','affinity','generation'},'pause':{'generation'},'close':{'drain'},'ack_result':{'request_sha256'},'discard':{'request_sha256'},'result':set()}
  if a not in allowed or set(c)!={'id','action'}|allowed[a]:raise ValueError('Exact command fields required; unexpected/expected answers forbidden')
  if a=='inventory':self.reply(c,inventory=self.load_inventory());return
  if a=='status':self.reply(c);return
  if a=='evidence':self.reply(c,evidence=self.evidence());return
  if c.get('id')=='eof'and a=='close':self.eof_requested=True;return
  if self.pending:raise ValueError('Boundary mutation already pending')
  if a in('execute','probe'):
   self.new_work(c,a=='probe')
   if self.enabled:
    if not self.admit_budget():self.budget_refused=True;self.state='BUDGET_PAUSED'
    else:self.start_workers();self.state='RUNNING'
   self.reply(c);return
  if a in('configure','pause'):
   g=c.get('generation')
   if type(g)!=int or g<=self.generation:raise ValueError('Strictly increasing session generation required')
   if a=='configure':
    if type(c.get('enabled'))!=bool or type(c.get('threads'))!=int:raise ValueError('enabled boolean andthreads integer required')
    n=c['threads'];aff=c.get('affinity')
    if not 1<=n<=self.inv['maximum_workers'] or type(aff)!=list or len(aff)!=n or any(type(x)!=int or x not in self.inv['available_cpus']for x in aff)or len(set(aff))!=n:raise ValueError('Dedicated CPU affinity/25percent reserve refused')
   self.pending=c;self.state='DRAINING_CONTROL';return
  if a=='close':
   if c.get('drain')is not True:raise ValueError('close requires drain=true')
   self.pending=c;self.state='DRAINING_CLOSE';return
  if a in('ack_result','discard'):
   if c.get('request_sha256')!=(digest(self.request)if self.request else None)or self.request is None:raise ValueError('Explicit matching request digest required')
   if self.workers or any(w.task for w in self.workers.values()):raise ValueError('Pause anddrain before clearing assignment')
   if a=='ack_result'and self.result is None:raise ValueError('No complete result toacknowledge')
   if a=='discard'and self.result is not None:raise ValueError('Unconsumed result requires ack_result')
   self.request=None;self.parts=[];self.counts=[];self.best=[];self.tests=0;self.result=None;self.total=self.committed=self.next_offset=0;self.enabled=False;self.state='IDLE';self.persist();self.reply(c);return
  if a=='result':
   if self.result is None:raise ValueError('No complete result; partial prefix cannot become a normal report')
   self.reply(c,result=dict(self.result,evidence=self.evidence()));return
  raise ValueError('Unsupported action')
 def run(self):
  def read():
   try:
    while True:
     line=sys.stdin.readline(32769)
     if not line:self.commands.put(dict(id='eof',action='close',drain=True));return
     if len(line)>32768 or not line.endswith('\n'):self.commands.put(dict(id=None,action='invalid',parse_error='Input line exceeds32KiB'));self.commands.put(dict(id='eof',action='close',drain=True));return
     try:self.commands.put(json.loads(line))
     except Exception:self.commands.put(dict(id=None,action='invalid',parse_error='Invalid JSON'))
   except BaseException:self.commands.put(dict(id='eof',action='close',drain=True))
  threading.Thread(target=read,daemon=True).start();emit(dict(event='ready',schema=SCHEMA,compute_started=False))
  lease=ControlLease()
  try:
   while not self.closed:
    self.pump(.005)
    if lease.closing_since is None:
     try:
      c=self.commands.get_nowait()
      try:self.command(c);lease.observe()
      except Exception as e:self.reply(c if isinstance(c,dict)else {},False,e)
     except queue.Empty:pass
    if not self.closed and lease.close_due(self.eof_requested):
     self.enabled=False;self.state='DRAINING_CLOSE'
     # Cancel an unacknowledged control mutation rather than enabling work
     # after its owner vanished. Only already-closed pieces may be persisted.
     self.pending=dict(id='eof',action='close',drain=True)
     if lease.drain_expired()and any(w.task for w in self.workers.values()):raise TimeoutError('CPU control lease expired; closed prefix retained')
    self.finish_pending()
    self.dispatch()
  except BaseException as e:
   self.failure=str(e);self.enabled=False;self.state='FAULT'
   for w in self.workers.values():w.terminate()
   self.workers={};self.completed={};self.next_offset=self.committed
   try:self.persist()
   except Exception:pass
   try:
    failure=dict(schema='PGN_CPU_LAST_FAILURE_V1',observed_epoch=time.time(),request_sha256=digest(self.request)if self.request else None,completed_positions=str(self.committed),error_type=type(e).__name__,message=str(e)[:1024],network_accepted=False)
    target=self.workspace/'last-failure.json';tmp=target.with_suffix('.pending');data=json.dumps(failure,separators=(',',':')).encode();assert len(data)<4096
    with open(tmp,'wb')as f:f.write(data);f.flush();os.fsync(f.fileno())
    replace_checkpoint(tmp,target)
   except Exception:pass
   if self.pending:self.reply(self.pending,False,e)
   emit(dict(event='worker_error',message=str(e)[:300],network_accepted=False))
  finally:
   for w in self.workers.values():w.terminate()
   self.job.close();self.lock.close()
  return 1 if self.failure else 0
def main():
 p=argparse.ArgumentParser();p.add_argument('--workspace',required=True,type=pathlib.Path);p.add_argument('--engine',type=pathlib.Path);p.add_argument('--inventory',type=pathlib.Path);p.add_argument('--piece-positions',type=int,default=10000000);p.add_argument('--scalar',action='store_true');p.add_argument('--large-pages',choices=['off','auto','required'],default='auto');p.add_argument('--initial-threads',type=int,default=1,help='Initial paused thread budget; never enables computation');a=p.parse_args();resource=pathlib.Path(getattr(sys,'_MEIPASS',pathlib.Path(__file__).resolve().parent));suffix='.exe'if os.name=='nt'else ''
 a.engine=a.engine or resource/'native'/('horizon-cpu-shared68-worker'+suffix);a.inventory=a.inventory or resource/'native'/('horizon-cpu-inventory'+suffix)
 if not 1<=a.initial_threads<=256:raise ValueError('Initial thread budget must be1..256')
 if not 1<=a.piece_positions<=100000000:raise ValueError('Piece size must be1..100M')
 return Service(a).run()
if __name__=='__main__':
 try:sys.exit(main())
 except Exception as e:print('CPU_SERVICE_STARTUP_ERROR:'+str(e)[:300],file=sys.stderr);sys.exit(2)
