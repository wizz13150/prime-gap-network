"""CPU/file/WAL-only output binding and recovery; never launches a worker/GPU."""
from pathlib import Path
import hashlib,json,sys,tempfile,unittest
from types import SimpleNamespace
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import pgn_output_spool as spool
import pgn_owner_recovery as recovery
from pgn_resident import Job
from horizon_engine_host import close_durable_output

TOKEN='a'*32;NONCE='b'*32;IMAGE='AB'*32;CONFIG='CD'*32
def dump(path,value):path.write_text(json.dumps(value),encoding='utf-8')
class Store:
 def __init__(self,folder):
  self.folder=folder;self.sequence=3;self.mutations=[]
  self.identity=dict(worker='worker-k8.exe',worker_sha256=IMAGE,start='100',length=2,profile_k=8,gap_min=30,emit_min_gap=30,base_positions=0,base_tests=0)
  self.meta=dict(session_id='c'*32,current_chunk=dict(identity=self.identity,instance=dict(pid=7,birth='8',job_nonce=NONCE)),positions=0,tests=0)
 def configuration_hash(self):return CONFIG
 def record_gap(self,*args):self.mutations.append(('gap',args))
 def accepted_progress(self,*args):self.mutations.append(('progress',args));self.meta.update(positions=args[0],tests=args[1])
 def recovery_rewind(self,*args):self.mutations.append(('rewind',args))
 def closed(self,*args,**kwargs):self.mutations.append(('closed',args))

class BindingTests(unittest.TestCase):
 def setUp(self):
  self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup);self.folder=Path(self.temp.name);self.root=self.folder/('native-owner-'+TOKEN);self.root.mkdir();self.directory=self.folder/'resident';self.directory.mkdir();self.store=Store(self.folder)
  self.command=['worker-k8.exe','--engine','walk','--start','100','--length','2','--gap-min','30','--emit-min-gap','30','--device','0','--state',str(self.folder/'native-checkpoint.bin'),'--stop-file',str(self.folder/'stop.request')]
  self.wire=list(self.command);self.wire[self.wire.index('--start')+1]='1';self.wire+=['--resident-directory',str(self.directory)]
  self.identity=dict(pid=7,birth='8',job_nonce=NONCE,image_sha256=IMAGE,configuration_sha256=CONFIG,session_id=self.store.meta['session_id'],profile_k=8,emit_min_gap=30,wal_sequence=2)
  self.current=self.folder/'current.json';dump(self.current,self.identity)
  dump(self.root/'bootstrap.json',dict(token=TOKEN,image_sha256=IMAGE,command=self.wire));dump(self.root/'ready.json',dict(token=TOKEN,image_sha256=IMAGE,pid=7,birth='8'))
 def fixture(self,v2=True,retire_prior=False):
  prior=b'GAP merit=1 gap=32 lower=1 upper=33 verified=1\n[pgn-counters] positions=1 gaps=1 tests=1\n[phase0-gpu] --- end ---\n{"plan":true}\n'
  current=b'GAP merit=1 gap=32 lower=100 upper=132 verified=1\n[pgn-counters] positions=2 gaps=1 tests=5\n[phase0-gpu] --- end ---\n'
  final=dict(token=TOKEN,image_sha256=IMAGE,started=True,closed=True,orphan=True,error=None,exit=0)
  if v2:
   w=spool.Writer(self.root/'output',TOKEN,64,4096)
   for line in prior.splitlines(keepends=True):w.append(line)
   if retire_prior:
    spool.acknowledge(self.root/'output',TOKEN,w.offset);w.collect()
   start=w.offset;binding=spool.commit_job(self.root,TOKEN,NONCE,2,self.command,start,self.current);spool.dispatch_job(self.root,binding)
   for line in current.splitlines(keepends=True):w.append(line)
   seal=w.finish();dump(self.directory/'2.done.json',dict(schema='SCOUT_RESIDENT_JOB_V1',sequence=2,nonce=NONCE,pid=7,exit=0,wall_ns=1))
   final.update(schema='PGN_OWNER_OUTPUT_V2',output_spool=seal,output_job=spool.seal_job(self.root,TOKEN,self.wire))
  else:
   dump(self.root/'bootstrap.json',dict(token=TOKEN,image_sha256=IMAGE,command=self.command));dump(self.root/'committed-identity.json',self.identity);(self.root/'stdout.log').write_bytes(current)
   final.update(stdout_bytes=len(current),stdout_sha256=hashlib.sha256(current).hexdigest(),committed_identity_sha256=hashlib.sha256((self.root/'committed-identity.json').read_bytes()).hexdigest())
  dump(self.root/'done.json',final);dump(self.folder/'native-owner-recovery.json',dict(token=TOKEN,owner_folder=self.root.name));return final
 def test_V2_current_job2_not_bootstrap_or_plan_output(self):
  self.fixture();self.assertTrue(recovery.recover(self.store));self.assertEqual(self.store.mutations[0],('gap',(100,132,'Native_verified_detached_recovery')));self.assertEqual([x[0] for x in self.store.mutations],['gap','progress','closed'])
 def test_V1_legacy_recovery_unchanged(self):
  self.fixture(False);self.assertTrue(recovery.recover(self.store));self.assertEqual(self.store.mutations[0][1][:2],(100,132))
 def test_prior_durable_ACK_then_crash_job2_retains_exact_current_tail(self):
  final=self.fixture(retire_prior=True);self.assertGreater(final['output_spool']['base_offset'],0)
  self.assertTrue(recovery.recover(self.store));self.assertEqual(self.store.mutations[0][1][:2],(100,132))
 def test_current_job_already_ACKed_is_not_replayed_after_crash(self):
  final=self.fixture();seal=final['output_spool'];final['output_spool']=dict(seal,base_offset=seal['end_offset'],base_link=seal['segments'][-1]['link'],segments=[]);dump(self.root/'done.json',final)
  self.assertFalse(recovery.recover(self.store));self.assertEqual(self.store.mutations,[])
 def test_unknown_owner_schema_is_not_legacy_fallback(self):
  final=self.fixture(False);final['schema']='UNKNOWN_NEXT_VERSION';dump(self.root/'done.json',final)
  self.assertFalse(recovery.recover(self.store));self.assertEqual(self.store.mutations,[])
 def test_segment_tamper_rejected_before_any_session_mutation(self):
  final=self.fixture();entry=final['output_spool']['segments'][-1];path=self.root/'output'/spool._name(entry['index']);raw=path.read_bytes();path.write_bytes(b'!'+raw[1:])
  with self.assertRaises(spool.SpoolError):recovery.recover(self.store)
  self.assertEqual(self.store.mutations,[])
 def test_changed_binding_rejected_before_mutation(self):
  final=self.fixture();binding=final['output_job']['binding'];binding['command'][binding['command'].index('--start')+1]='101';dump(self.root/'done.json',final)
  self.assertFalse(recovery.recover(self.store));self.assertEqual(self.store.mutations,[])
 def test_committed_next_job_never_promotes_previous_execution(self):
  self.fixture();new='d'*32;identity=dict(self.identity,job_nonce=new);dump(self.current,identity);spool.commit_job(self.root,TOKEN,new,3,self.command,0,self.current);self.store.meta['current_chunk']['instance']['job_nonce']=new
  self.assertFalse(recovery.recover(self.store));self.assertEqual(self.store.mutations,[])
 def test_dispatch_intent_without_native_receipt_is_not_execution(self):
  self.fixture();new='d'*32;dump(self.current,dict(self.identity,job_nonce=new));binding=spool.commit_job(self.root,TOKEN,new,3,self.command,0,self.current);spool.dispatch_job(self.root,binding)
  self.assertIsNone(spool.seal_job(self.root,TOKEN,self.wire))
 def test_plan_cannot_replace_calculation_binding(self):
  self.fixture();before=(self.root/'dispatched-binding.json').read_bytes()
  with self.assertRaises(spool.SpoolError):spool.commit_job(self.root,TOKEN,NONCE,3,['worker-k8.exe','--plan'],0,self.current)
  self.assertEqual((self.root/'dispatched-binding.json').read_bytes(),before)
 def test_close_alone_and_crash_after_read_never_ACK(self):
  w=spool.Writer(self.root/'output',TOKEN,128,4096);w.append(b'[phase0-gpu] --- end ---\n');w.finish();reader=spool.Reader(self.root/'output',lambda:True);line=next(reader)
  owner=SimpleNamespace(pid=7,birth='8',poll=lambda:None,acknowledge_output=lambda end:reader.acknowledge(TOKEN,end))
  manager=SimpleNamespace(owner=owner,errors=[],directory=self.directory,shutdown=lambda **kwargs:None)
  job=Job(manager,2,NONCE,self.command);job.poll=lambda:0;job.lines.put(line);job.lines.put(None);list(job.stdout);job.close()
  self.assertFalse((self.root/'output/ack.json').exists());self.assertTrue(any((self.root/'output').glob('*.log')))
  class FailedStore:
   def closed(self,*args,**kwargs):raise OSError('journal closed did not commit')
  with self.assertRaises(OSError):close_durable_output(FailedStore(),job,'PAUSED',0,0)
  self.assertFalse((self.root/'output/ack.json').exists());reader.close()
 def test_real_SessionStore_closed_WAL_precedes_ACK_and_survives_reload(self):
  from pgn_session import SessionStore,read_session_metadata
  from pgn_native_contract import fields
  first=1<<66;cfg=dict(start=str(first),end=str(first+2),bins=1,report_top=10,**fields(first,first+2))
  folder=self.folder/'session';store=SessionStore.create(folder,cfg,{'uuid':'CPU-QA-no-device'})
  try:
   w=spool.Writer(self.root/'output',TOKEN,64,256);w.append(b'x'*63+b'\n');w.finish();reader=spool.Reader(self.root/'output',lambda:True);list(reader)
   def ACK():
    self.assertEqual(read_session_metadata(folder)['state'],'PAUSED')
    self.assertEqual(store.gaps.meta('state'),'PAUSED')
    return reader.acknowledge(TOKEN,64)
   close_durable_output(store,SimpleNamespace(acknowledge_output=ACK),'PAUSED',0,0)
   w.collect();self.assertEqual(list((self.root/'output').glob('*.log')),[]);reader.close()
  finally:store.close()
  reopened=SessionStore.load(folder)
  try:self.assertEqual(reopened.meta['state'],'PAUSED')
  finally:reopened.close()
 def test_real_controller_code_seals_segmented_fake_process_without_GPU_API(self):
  import ctypes as C
  from ctypes import wintypes as T
  import pgn_owned, pgn_detached_worker as detached
  class Fn:
   def __init__(self,callback):self.callback=callback
   def __call__(self,*args):return self.callback(*args)
  def in_job(handle,job,pointer):C.cast(pointer,C.POINTER(T.BOOL))[0]=False;return True
  kernel=SimpleNamespace(IsProcessInJob=Fn(in_job),GetCurrentProcess=Fn(lambda:1))
  class Lease:
   def __init__(self,*args):pass
   def acquire(self):return self
   def release(self):pass
  class Process:
   pid=7;birth='8'
   def __init__(self,*args,**kwargs):self.code=None
   def commit_identity(self,path):pass
   def resume(self):pass
   def poll(self):return self.code
   def wait(self):return self.code
   def terminate(self):self.code=1
   def close(self):pass
   @property
   def stdout(self):
    yield 'controller-line\n';yield '[phase0-gpu] --- end ---\n';self.code=0
  identity_path=self.folder/'native-owner-current.json';dump(identity_path,self.identity)
  binding=spool.commit_job(self.root,TOKEN,NONCE,0,self.command,0,identity_path);spool.dispatch_job(self.root,binding)
  wire=list(self.command)+['--resident-directory',str(self.directory)]
  bootstrap=self.root/'bootstrap.json';dump(bootstrap,dict(token=TOKEN,command=wire,image_sha256=IMAGE,environment={},gpu_uuid='QA-NOT-A-DEVICE',parent={'pid':1,'birth':'1'}));dump(self.root/'resume.json',dict(token=TOKEN,identity=str(identity_path)))
  dump(self.directory/'0.done.json',dict(schema='SCOUT_RESIDENT_JOB_V1',sequence=0,nonce=NONCE,pid=7,exit=0,wall_ns=1))
  with patch.object(pgn_owned,'_kernel',return_value=(kernel,T)),patch.object(pgn_owned,'GpuLease',Lease),patch.object(pgn_owned,'WindowsOwnedProcess',Process),patch.object(pgn_owned,'image_hash',return_value=IMAGE),patch.object(pgn_owned,'_recorded_alive',return_value=True):
   self.assertEqual(detached.boot_main(bootstrap),0)
  final=json.loads((self.root/'done.json').read_text());self.assertEqual(final['schema'],'PGN_OWNER_OUTPUT_V2');self.assertEqual(final['output_job']['binding'],binding)
  paths=spool.verified_segments(self.root/'output',final['output_spool'],TOKEN);self.assertEqual(b''.join(p.read_bytes() for p in paths),b'controller-line\n[phase0-gpu] --- end ---\n');self.assertFalse((self.root/'stdout.log').exists())

class NormalPauseTests(unittest.TestCase):
 """Actual SessionStore/EngineService; all native work is a CPU fake."""
 def make_paused(self):
  import struct
  from pgn_session import SessionStore,read_session_metadata
  from pgn_native_contract import fields,magic
  from pgn_resident import WorkReceipt
  temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup)
  base=Path(temporary.name);folder=base/'tickets'/('2'*64);folder.parent.mkdir()
  worker=base/'worker-k8.exe';worker.write_bytes(b'QA inert worker; never executable')
  first=(1<<66)+1000;cfg=dict(start=str(first),end=str(first+4),ticket_id='2'*64,bins=1,report_top=10,**fields(first,first+4))
  store=SessionStore.create(folder,cfg,{'uuid':'CPU-QA-no-device'})
  self.addCleanup(store.close)
  image=hashlib.sha256(worker.read_bytes()).hexdigest().upper();source='12'*32
  gap=cfg['effective_gap_min'];chunk=store.bind_chunk(start=first,length=4,worker=worker,worker_sha256=image,profile_k=8,base_positions=0,base_tests=0,gap_min=gap,threshold_policy_sha256=cfg['threshold_policy']['sha256'],worker_source_sha256=source)
  store.started(7,'8',job_nonce=NONCE)
  owner=folder.parent/('1'*64)/('native-owner-'+TOKEN);owner.mkdir(parents=True)
  locator=dict(token=TOKEN,owner_ticket_id='1'*64,sequence=2,nonce=NONCE,binding='job.json',binding_sha256='1'*64)
  chunk['instance']['output_owner']=locator;store.append('instance_started',instance=chunk['instance']);store.save()
  cp=struct.pack('<8s7Q',magic(gap),first%(1<<64),first>>64,4,1,5,0,0)
  (folder/'native-checkpoint.bin').write_bytes(cp);store.accepted_progress(1,5);store.capture_closed_checkpoint(folder/'native-checkpoint.bin',1,5)
  command=(str(worker),'--start',str(first),'--length','4','--gap-min',str(gap))
  receipt=WorkReceipt(7,'8',NONCE,2,command,0);self.assertTrue(receipt.matches(chunk))
  store.closed('PAUSED',1,0,instance_closed=True,work_receipt=receipt)
  dump(owner/'done.json',dict(schema='PGN_OWNER_OUTPUT_V2',token=TOKEN,closed=True,started=True,orphan=False,exit=0,error=None))
  self.assertEqual(read_session_metadata(folder)['current_chunk']['instance']['work_closed'],True)
  store.close();store=SessionStore.load(folder);self.addCleanup(store.close)
  return store,worker,source,cp,owner
 def service(self,store,worker,source,cp):
  import struct
  import horizon_engine_host as host
  from pgn_native_contract import magic
  from pgn_publications import WorkRates
  from pgn_resident import WorkReceipt
  test=self;created=[];acks=[]
  class FakeJob:
   pid=7;birth='8';first=False
   def __init__(self,args):
    self.args=list(args);self.nonce=('d' if not created else 'e')*32;self.code=None
    self.before=(store.folder/'native-checkpoint.bin').read_bytes()
    expected=store.meta['current_chunk']['trusted_checkpoint'];self.position=expected['native_positions']+1
    test.assertEqual(hashlib.sha256(self.before).hexdigest(),expected['sha256'])
   def commit_identity(self,path):test.assertEqual(json.loads(Path(path).read_bytes())['job_nonce'],self.nonce)
   def output_recovery_locator(self):return None
   def resume(self):
    test.assertFalse((store.folder/'stop.request').exists());identity=store.meta['current_chunk']['identity'];first=int(identity['start'])
    (store.folder/'native-checkpoint.bin').write_bytes(struct.pack('<8s7Q',magic(identity['gap_min']),first%(1<<64),first>>64,int(identity['length']),self.position,self.position*5,0,0))
   @property
   def stdout(self):
    yield f'[pgn-counters] positions={self.position} gaps=0 tests={self.position*5}\n'
    yield f'[phase0-gpu] stopped positions={self.position}\n';self.code=0
   def poll(self):return self.code
   def wait(self,timeout=None):self.code=0;return 0
   def clock(self,*args):return SimpleNamespace(finish=lambda:(.01,None))
   def close(self):test.assertEqual(self.code,0)
   def process_exited(self):return True
   def work_receipt(self):return WorkReceipt(self.pid,self.birth,self.nonce,len(created),tuple(self.args),0)
   def acknowledge_output(self):
    from pgn_session import read_session_metadata
    durable=read_session_metadata(store.folder);test.assertEqual(durable['state'],'PAUSED');test.assertTrue(durable['current_chunk']['instance']['work_closed']);test.assertEqual(durable['positions'],self.position);acks.append(self.position)
  def create(command,environment):
   job=FakeJob(command);created.append(job);return job
  service=host.EngineService.__new__(host.EngineService);service.root=store.folder.parent.parent;service.environment={};service.observer=lambda frame:None
  service.manager=SimpleNamespace(create=create,shutdown=lambda **kwargs:None);service.workers=lambda:[(8,worker)]
  service.rates=WorkRates();service.rate_identity=None;service.checked={hashlib.sha256(worker.read_bytes()).hexdigest().upper()};service.boundary=lambda *args:None
  return service,created,acks
 def test_real_durable_normal_pause_recovery_skips_without_progress_replay(self):
  store,worker,source,cp,owner=self.make_paused();before=json.dumps(store.meta,sort_keys=True);sequence=store.sequence
  self.assertFalse(recovery.recover(store));self.assertEqual(store.sequence,sequence);self.assertEqual(json.dumps(store.meta,sort_keys=True),before)
  (store.folder/'native-checkpoint.bin').write_bytes(b'untrusted newer bytes')
  self.assertEqual(store.restore_checkpoint(store.folder/'native-checkpoint.bin'),(1,5));self.assertEqual((store.folder/'native-checkpoint.bin').read_bytes(),cp)
 def test_real_engine_second_run_and_extra_resume_restore_verified_checkpoint(self):
  import horizon_engine_host as host,pgn_session
  store,worker,source,cp,owner=self.make_paused();service,created,acks=self.service(store,worker,source,cp)
  with patch.object(pgn_session,'module_metadata',return_value={'source_manifest_sha256':source,'profile_k':8}),patch.object(host,'run_checked',side_effect=AssertionError('GPU diagnostic forbidden')),patch.object(host,'create_owned',side_effect=AssertionError('Native launch forbidden')):
   for expected in (2,3):
    (store.folder/'native-checkpoint.bin').write_bytes(b'untrusted newer bytes')
    result=service._run(store,4);self.assertEqual(result['state'],'PAUSED');self.assertEqual((store.meta['positions'],store.meta['tests']),(expected,expected*5));self.assertTrue(store.meta['current_chunk']['instance']['work_closed'])
  self.assertEqual(len(created),2);self.assertEqual(acks,[2,3]);self.assertEqual(created[0].before,cp)
 def test_closed_flags_never_bypass_trusted_checkpoint_tamper(self):
  import horizon_engine_host as host,pgn_session
  store,worker,source,cp,owner=self.make_paused();service,created,acks=self.service(store,worker,source,cp)
  (store.folder/'trusted-current-checkpoint.bin').write_bytes(b'changed trusted bytes')
  with self.assertRaisesRegex(ValueError,'Trusted checkpoint bytes changed'):service._run(store,4)
  self.assertEqual(created,[]);self.assertEqual(acks,[]);self.assertEqual((store.meta['positions'],store.meta['tests']),(1,5))
 def test_normal_receipt_without_strict_closed_flags_is_still_refused(self):
  store,worker,source,cp,owner=self.make_paused()
  for closed,work_closed in [(False,True),(True,False),(True,None),(1,True),(True,1),(True,'true')]:
   with self.subTest(closed=closed,work_closed=work_closed):
    instance=store.meta['current_chunk']['instance'];instance['closed']=closed;instance['work_closed']=work_closed
    with self.assertRaisesRegex(ValueError,'Detached owner has not safely closed'):recovery.recover(store)
 def test_missing_owner_done_without_work_closure_adds_no_progress(self):
  store,worker,source,cp,owner=self.make_paused();store.meta['current_chunk']['instance'].pop('work_closed');(owner/'done.json').unlink();sequence=store.sequence
  self.assertFalse(recovery.recover(store));self.assertEqual(store.sequence,sequence);self.assertEqual((store.meta['positions'],store.meta['tests']),(1,5))


if __name__=='__main__':unittest.main()
