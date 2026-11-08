"""Bounded native transport and OS controls; standard library only."""
import ctypes,hashlib,json,os,queue,subprocess,threading,time
from collections import deque
from runtime_metrics import system_cpu_counters
from native_image_identity import HASHES
def verify(path,sha):
 if hashlib.sha256(path.read_bytes()).hexdigest()!=sha:raise ValueError('Native payload hash mismatch')
def inventory(path):
 verify(path,HASHES[os.name][1]);r=subprocess.run([str(path),'--inventory'],capture_output=True,text=True,timeout=5,creationflags=(0x08000000|0x00004000) if os.name=='nt'else 0)
 if r.returncode or len(r.stdout)>32768:raise ValueError('Native inventory failed')
 v=json.loads(r.stdout)
 if v.get('compute_started')is not False:raise ValueError('Inventory did not preserve idle contract')
 return v
class Load:
 def __init__(self):self.reset()
 def reset(self):
  self.prev=self.sample();self.when=time.monotonic();self.value=None;self.window=deque([(self.when,self.prev)])
 def sample(self):return system_cpu_counters()
 def update(self):
  now=time.monotonic()
  if now-self.when<.25:return self.value
  cur=self.sample();self.window.append((now,cur))
  # A one-second counter window tolerates short interactive bursts while
  # retaining the same75percent sustained-load limit and dedicated reserve.
  while len(self.window)>2 and self.window[1][0]<=now-1.:self.window.popleft()
  first=self.window[0][1];d=cur[1]-first[1]
  if d>0:self.value=max(0.,min(100.,100*(1-(cur[0]-first[0])/d)))
  self.prev=cur;self.when=now;return self.value
class Job:
 def __init__(self):
  self.handle=None
  if os.name=='nt':
   self.k=ctypes.WinDLL('kernel32',use_last_error=True);self.k.CreateJobObjectW.argtypes=[ctypes.c_void_p,ctypes.c_wchar_p];self.k.CreateJobObjectW.restype=ctypes.c_void_p;self.k.SetInformationJobObject.argtypes=[ctypes.c_void_p,ctypes.c_int,ctypes.c_void_p,ctypes.c_uint32];self.k.AssignProcessToJobObject.argtypes=[ctypes.c_void_p,ctypes.c_void_p];self.k.CloseHandle.argtypes=[ctypes.c_void_p]
   self.handle=self.k.CreateJobObjectW(None,None)
   if not self.handle:raise ctypes.WinError(ctypes.get_last_error())
   buf=ctypes.create_string_buffer(144);ctypes.c_uint32.from_buffer(buf,16).value=0x2000
   if not self.k.SetInformationJobObject(self.handle,9,buf,144):raise ctypes.WinError(ctypes.get_last_error())
 def assign(self,p):
  if self.handle and not self.k.AssignProcessToJobObject(self.handle,ctypes.c_void_p(int(p._handle))):p.terminate();p.wait();raise ctypes.WinError(ctypes.get_last_error())
 def close(self):
  if self.handle:self.k.CloseHandle(self.handle);self.handle=None
class Worker:
 def __init__(self,path,cpu,simd,events,job,page_mode='off'):
  self.cpu=cpu;self.task=None;self.ready=False;self.stopped=False;self.positions=0;self.native_seconds=0.0
  self.memory_pages={};self.memory_sample_time=0.;self.memory_query_pending=False
  self.p=subprocess.Popen([str(path),'--owned-piece','--cpu',str(cpu)]+(['--simd4']if simd else [])+['--tiled','--large-pages='+page_mode],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,bufsize=1,creationflags=(0x08000000|0x00004000) if os.name=='nt'else 0)
  job.assign(self.p)
  def read():
   try:
    while True:
     line=self.p.stdout.readline(32769)
     if not line:break
     if len(line)>32768 or not line.endswith('\n'):raise ValueError('Worker output line exceeds budget')
     events.put((self,json.loads(line)))
   except BaseException as e:events.put((self,dict(type='transport_error',message=str(e))))
   finally:events.put((self,None))
  threading.Thread(target=read,daemon=True).start()
 def send(self,line):self.p.stdin.write(line+'\n');self.p.stdin.flush()
 def terminate(self):
  if self.p.poll()is None:
   self.p.terminate()
   try:self.p.wait(timeout=3)
   except subprocess.TimeoutExpired:self.p.kill();self.p.wait()
