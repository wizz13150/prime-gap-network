"""Real controller/recovery code, two ticket folders, fake process and no GPU."""
from pathlib import Path
import ctypes as C,hashlib,importlib.util,json,struct,sys,tempfile,threading,time,unittest
from ctypes import wintypes as T
from types import SimpleNamespace
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1];sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol')]
import pgn_output_spool as spool,pgn_detached_worker as fixed,pgn_owned,pgn_owner_recovery as recovery,pgn_session
from pgn_native_contract import PROTOCOL
TOKEN='a'*32;NONCE='b'*32;IMAGE='AB'*32
def dump(p,v):p.write_text(json.dumps(v),encoding='utf-8')
class Fn:
 def __init__(self,fn):self.fn=fn
 def __call__(self,*a):return self.fn(*a)
class CrossTicketTests(unittest.TestCase):
 def run_controller(self,module,orphan=True,complete=False,retired=None):
  temporary=tempfile.TemporaryDirectory();self.addCleanup(temporary.cleanup);base=Path(temporary.name)/'tickets';one=base/('1'*64);two=base/('2'*64);one.mkdir(parents=True);two.mkdir();owner=one/('native-owner-'+TOKEN);owner.mkdir();resident=base.parent/'resident';resident.mkdir()
  command=lambda folder,start,length:['worker-k8.exe','--engine','walk','--start',str(start),'--length',str(length),'--gap-min','30','--emit-min-gap','30','--device','0','--state',str(folder/'native-checkpoint.bin'),'--stop-file',str(folder/'stop.request')]
  initial=command(one,1,1);current=command(two,100,2);wire=initial+['--resident-directory',str(resident)];cfg={};config=hashlib.sha256(spool._encoded(cfg)).hexdigest();identity=dict(pid=7,birth='8',job_nonce=NONCE,image_sha256=IMAGE,configuration_sha256=config,session_id='c'*32,profile_k=8,emit_min_gap=30,wal_sequence=2,ticket_id=two.name)
  current_file=two/'native-owner-current.json';dump(current_file,identity);dump(one/'native-owner-current.json',dict(identity,ticket_id=one.name))
  prior='GAP merit=1 gap=32 lower=1 upper=33 verified=1\n[pgn-counters] positions=1 gaps=1 tests=1\n[phase0-gpu] --- end ---\n'
  binding=spool.commit_job(owner,TOKEN,NONCE,2,current,len(prior.encode()),current_file);spool.dispatch_job(owner,binding);locator=spool.recovery_locator(owner,binding)
  chunk_identity=dict(worker='worker-k8.exe',worker_sha256=IMAGE,start='100',length=2,profile_k=8,gap_min=30,emit_min_gap=30,base_positions=0,base_tests=0,native_protocol=PROTOCOL)
  meta=dict(configuration=cfg,session_id='c'*32,current_chunk=dict(index=0,identity=chunk_identity,instance=dict(pid=7,birth='8',job_nonce=NONCE,output_owner=locator)),positions=0,tests=0)
  cp1=struct.pack('<8s7Q',b'PGNS'+struct.pack('<I',30),1,0,1,0,0,0,0);cp2=struct.pack('<8s7Q',b'PGNS'+struct.pack('<I',30),100,0,2,1,5,0,0);(one/'native-checkpoint.bin').write_bytes(cp1);(two/'native-checkpoint.bin').write_bytes(cp2)
  bootstrap=owner/'bootstrap.json';dump(bootstrap,dict(token=TOKEN,command=wire,image_sha256=IMAGE,environment={},gpu_uuid='QA-NOT-A-DEVICE',parent=dict(pid=1,birth='1')));dump(owner/'resume.json',dict(token=TOKEN,identity=str(one/'native-owner-current.json')))
  def in_job(h,j,p):C.cast(p,C.POINTER(T.BOOL))[0]=False;return True
  kernel=SimpleNamespace(IsProcessInJob=Fn(in_job),GetCurrentProcess=Fn(lambda:1))
  class Lease:
   def __init__(self,*a):pass
   def acquire(self):return self
   def release(self):pass
  class Process:
   pid=7;birth='8'
   def __init__(self,*a,**kw):self.code=None
   def commit_identity(self,p):pass
   def resume(self):pass
   def poll(self):return self.code
   def wait(self):return self.code
   def terminate(self):self.code=1
   def close(self):pass
   @property
   def stdout(self):
    for line in prior.splitlines(keepends=True):yield line
    if complete:
     (two/'native-checkpoint.bin').unlink()
     yield '[pgn-counters] positions=2 gaps=0 tests=5\n';yield '[phase0-gpu] --- end ---\n'
    else:
     yield '[pgn-counters] positions=1 gaps=0 tests=5\n';yield '[phase0-gpu] stopped positions=1\n'
    if retired:
     import copy,sqlite3
     previous=copy.deepcopy(meta['current_chunk']);previous['instance']['work_closed']=True
     if retired=='uncommitted':previous['instance']['work_closed']=False
     if retired=='wrong_nonce':previous['instance']['job_nonce']='e'*32
     if retired=='wrong_interval':previous['identity']['start']='101'
     if retired=='wrong_worker':previous['identity']['worker_sha256']='EF'*32
     if retired=='wrong_owner':previous['instance']['output_owner']['token']='e'*32
     meta['closed_chunks']=[previous]if retired=='hot'else[]
     meta['current_chunk']=dict(index=1,identity=dict(chunk_identity,start='102'),instance=dict(pid=7,birth='8',job_nonce='d'*32,closed=False))
     db=sqlite3.connect(two/'session-index.sqlite3');db.execute('CREATE TABLE meta(k TEXT PRIMARY KEY,v TEXT NOT NULL)');db.execute('CREATE TABLE chunks(idx INTEGER PRIMARY KEY,value TEXT NOT NULL)');db.execute('INSERT INTO meta VALUES(?,?)',('binding',json.dumps(dict(session_id=meta['session_id'],configuration_hash=config))));db.execute('INSERT INTO chunks VALUES(?,?)',(0,json.dumps(previous)));db.commit();db.close()
    deadline=time.monotonic()+5
    while orphan and not any((f/'stop.request').exists() for f in (one,two)) and time.monotonic()<deadline:time.sleep(.001)
    dump(resident/'2.done.json',dict(schema='SCOUT_RESIDENT_JOB_V1',sequence=2,nonce=NONCE,pid=7,exit=0,wall_ns=1));self.code=0
  with patch.object(pgn_owned,'_kernel',return_value=(kernel,T)),patch.object(pgn_owned,'GpuLease',Lease),patch.object(pgn_owned,'WindowsOwnedProcess',Process),patch.object(pgn_owned,'image_hash',return_value=IMAGE),patch.object(pgn_owned,'_recorded_alive',return_value=not orphan),patch.object(pgn_session,'read_session_metadata',side_effect=lambda folder:meta if Path(folder)==two else self.fail('Wrong session lookup')):
   module.boot_main(bootstrap)
  final=json.loads((owner/'done.json').read_text());return one,two,owner,final,meta,config,cp2
 def test_normal_owner_closes_after_exact_durable_work_and_next_unexecuted_chunk(self):
  for retired in ('hot','cold'):
   with self.subTest(retired=retired):
    one,two,owner,final,meta,config,cp2=self.run_controller(fixed,orphan=False,complete=True,retired=retired)
    self.assertTrue(final['closed']);self.assertIsNone(final['error'])
    self.assertEqual(final['output_job']['binding']['nonce'],NONCE)
    self.assertNotIn('recovery_ticket_id',final)
    self.assertEqual(meta['current_chunk']['instance']['job_nonce'],'d'*32)
 def test_retired_receipt_never_excuses_uncommitted_or_different_work(self):
  for retired in ('uncommitted','wrong_nonce','wrong_interval','wrong_worker','wrong_owner'):
   with self.subTest(retired=retired):
    one,two,owner,final,meta,config,cp2=self.run_controller(fixed,orphan=False,complete=True,retired=retired)
    self.assertIn('Output job seal',final['error']);self.assertNotIn('recovery_ticket_id',final)
 def test_closed_normal_owner_after_uncommitted_storage_failure_recovers_exact_job(self):
  for complete in (False,True):
   with self.subTest(complete=complete):
    one,two,owner,final,meta,config,cp2=self.run_controller(fixed,orphan=False,complete=complete)
    self.assertFalse(final['orphan']);self.assertTrue(final['closed']);self.assertEqual(final['exit'],0)
    meta['current_chunk']['instance']['closed']=True
    class Store:
     folder=two;sequence=3
     def __init__(self):self.meta=meta;self.changes=[]
     def configuration_hash(self):return config
     def capture_closed_checkpoint(self,*a):self.changes.append('checkpoint')
     def accepted_progress(self,*a):self.changes.append('progress');self.meta.update(positions=a[0],tests=a[1])
     def closed(self,*a,**kw):self.changes.append('closed')
    store=Store();self.assertTrue(recovery.recover(store))
    self.assertEqual(store.changes,(['progress','closed']if complete else['checkpoint','progress','closed']))
    self.assertEqual(store.meta['positions'],2 if complete else 1)
 def test_normal_close_does_not_admit_unsealed_or_wrong_job_output(self):
  for fault in ('closed','orphan','token','image','nonce','receipt','ticket'):
   with self.subTest(fault=fault):
    one,two,owner,final,meta,config,cp2=self.run_controller(fixed,orphan=False,complete=True)
    meta['current_chunk']['instance']['closed']=True
    if fault=='closed':final['closed']=False
    elif fault=='orphan':final['orphan']=0
    elif fault=='token':final['token']='f'*32
    elif fault=='image':final['image_sha256']='EF'*32
    elif fault=='nonce':meta['current_chunk']['instance']['job_nonce']='e'*32
    elif fault=='receipt':final['output_job']['work_receipt']['exit']=1
    elif fault=='ticket':final['recovery_ticket_id']=one.name
    dump(owner/'done.json',final)
    forbidden=lambda *a,**kw:self.fail('Invalid normal closure mutated durable progress')
    store=SimpleNamespace(folder=two,sequence=3,meta=meta,configuration_hash=lambda:config,
     capture_closed_checkpoint=forbidden,accepted_progress=forbidden,closed=forbidden,record_gap=forbidden)
    try:accepted=recovery.recover(store)
    except ValueError:accepted=False
    self.assertFalse(accepted)
 def test_retained_35gate_controller_reproduces_wrong_ticket_checkpoint_stop_receipt(self):
  baseline=ROOT/'tests/fixtures/output_spool_same_folder_owner.py'
  self.assertTrue(baseline.is_file());spec=importlib.util.spec_from_file_location('baseline_samefolder_owner',baseline);old=importlib.util.module_from_spec(spec);spec.loader.exec_module(old)
  one,two,owner,final,meta,config,cp2=self.run_controller(old)
  self.assertEqual(final['checkpoint'],str(one/'native-checkpoint.bin'));self.assertTrue((one/'stop.request').exists());self.assertFalse((two/'stop.request').exists());self.assertTrue((one/'native-owner-recovery.json').exists());self.assertFalse((two/'native-owner-recovery.json').exists());self.assertNotEqual(final['checkpoint_sha256'],hashlib.sha256(cp2).hexdigest().upper())
 def test_current_ticket2_partial_checkpoint_recovery_from_ticket1_physical_owner(self):
  one,two,owner,final,meta,config,cp2=self.run_controller(fixed)
  self.assertEqual(final.get('error'),None);self.assertEqual(final['checkpoint'],str(two/'native-checkpoint.bin'));self.assertEqual(final['checkpoint_sha256'],hashlib.sha256(cp2).hexdigest().upper());self.assertTrue((two/'stop.request').exists());self.assertFalse((one/'stop.request').exists());self.assertTrue((two/'native-owner-recovery.json').exists());self.assertFalse((one/'native-owner-recovery.json').exists())
  class Store:
   folder=two;sequence=3
   def __init__(self):self.meta=meta;self.changes=[]
   def configuration_hash(self):return config
   def capture_closed_checkpoint(self,*a):self.changes.append(('checkpoint',a))
   def accepted_progress(self,*a):self.changes.append(('progress',a));self.meta.update(positions=a[0],tests=a[1])
   def closed(self,*a,**kw):self.changes.append(('closed',a))
  store=Store();self.assertTrue(recovery.recover(store));self.assertEqual([row[0] for row in store.changes],['checkpoint','progress','closed']);self.assertEqual(store.meta['positions'],1)
 def test_cross_ticket_locator_cannot_choose_arbitrary_folder(self):
  one,two,owner,final,meta,config,cp2=self.run_controller(fixed)
  meta['current_chunk']['instance']['output_owner']['owner_ticket_id']='../outside'
  fake=SimpleNamespace(folder=two,meta=meta)
  with self.assertRaises(ValueError):recovery.recover(fake)
 def test_done_receipt_discovered_from_durable_locator_after_publication_crash(self):
  one,two,owner,final,meta,config,cp2=self.run_controller(fixed)
  (two/'native-owner-recovery.json').unlink()
  class Store:
   folder=two;sequence=3
   def __init__(self):self.meta=meta;self.changes=[]
   def configuration_hash(self):return config
   def capture_closed_checkpoint(self,*a):self.changes.append('checkpoint')
   def accepted_progress(self,*a):self.changes.append('progress');self.meta.update(positions=a[0],tests=a[1])
   def closed(self,*a,**kw):self.changes.append('closed')
  store=Store();self.assertTrue(recovery.recover(store));self.assertEqual(store.changes,['checkpoint','progress','closed'])
 def test_host_publishes_locator_in_real_active_chunk_WAL_not_only_JSON(self):
  from pgn_native_contract import fields
  from pgn_session import SessionStore,read_session_metadata
  from horizon_engine_host import commit_bound_output
  with tempfile.TemporaryDirectory() as temporary:
   folder=Path(temporary);first=1<<66;cfg=dict(start=str(first),end=str(first+2),bins=1,report_top=10,**fields(first,first+2));store=SessionStore.create(folder,cfg,{'uuid':'CPU-QA-no-device'})
   locator=dict(token=TOKEN,owner_ticket_id='1'*64,sequence=2,nonce=NONCE,binding='fixed',binding_sha256='f'*64)
   try:
    store.meta['current_chunk']=dict(identity={},instance=dict(pid=7,birth='8',job_nonce=NONCE))
    with store.gaps.db:store.gaps.put_meta('active_chunk',store.meta['current_chunk'])
    job=SimpleNamespace(commit_identity=lambda path:None,output_recovery_locator=lambda:locator)
    commit_bound_output(store,job,folder/'identity.json')
    self.assertEqual(read_session_metadata(folder)['current_chunk']['instance']['output_owner'],locator)
   finally:store.close()
   reopened=SessionStore.load(folder)
   try:self.assertEqual(reopened.meta['current_chunk']['instance']['output_owner'],locator)
   finally:reopened.close()

if __name__=='__main__':unittest.main()
