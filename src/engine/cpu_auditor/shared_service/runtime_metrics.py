"""Bounded, read-only CPU telemetry. It cannot dispatch or accept work."""
import ctypes,math,os,time
from collections import deque
from functools import lru_cache
from pathlib import Path

class PositionRateWindow:
 def __init__(self,seconds=5.):self.seconds=seconds;self.samples=deque()
 def observe(self,positions,active,now=None):
  now=time.monotonic()if now is None else now
  if not active:self.samples.clear();self.samples.append((now,positions));return 0.
  if self.samples and(now<self.samples[-1][0]or positions<self.samples[-1][1]):self.samples.clear()
  if not self.samples or now>self.samples[-1][0]:self.samples.append((now,positions))
  while len(self.samples)>2 and self.samples[1][0]<=now-self.seconds:self.samples.popleft()
  if len(self.samples)<2 or now-self.samples[0][0]<.25:return None
  return (positions-self.samples[0][1])/(now-self.samples[0][0])

@lru_cache(maxsize=1)
def windows_api():
 """Create ctypes types and function bindings once; POINTER caches type keys."""
 from ctypes import wintypes as W
 class FileTime(ctypes.Structure):
  _fields_=[('lo',W.DWORD),('hi',W.DWORD)]
 class ProcessMemory(ctypes.Structure):
  _fields_=[('cb',W.DWORD),('faults',W.DWORD)]+[(n,ctypes.c_size_t)for n in('peak_ws','ws','peak_paged','paged','peak_nonpaged','nonpaged','pagefile','peak_pagefile','private')]
 class SystemMemory(ctypes.Structure):
  _fields_=[('length',ctypes.c_uint32),('load',ctypes.c_uint32)]+[(n,ctypes.c_uint64)for n in('total','available','pagefile','available_pagefile','virtual','available_virtual','extended')]
 k=ctypes.WinDLL('kernel32',use_last_error=True);p=ctypes.WinDLL('psapi',use_last_error=True)
 k.GetCurrentProcess.restype=ctypes.c_void_p
 k.GetProcessTimes.argtypes=[ctypes.c_void_p]+[ctypes.POINTER(FileTime)]*4
 k.GetSystemTimes.argtypes=[ctypes.POINTER(FileTime)]*3
 k.GlobalMemoryStatusEx.argtypes=[ctypes.POINTER(SystemMemory)]
 p.GetProcessMemoryInfo.argtypes=[ctypes.c_void_p,ctypes.POINTER(ProcessMemory),W.DWORD]
 return k,p,FileTime,ProcessMemory,SystemMemory

def process_reading(pid,handle=None):
 if os.name=='nt':
  k,p,FT,Memory,_=windows_api();h=int(handle)if handle is not None else k.GetCurrentProcess()
  a,b,c,d=FT(),FT(),FT(),FT();m=Memory();m.cb=ctypes.sizeof(m)
  cpu=((c.hi<<32|c.lo)+(d.hi<<32|d.lo))/1e7 if k.GetProcessTimes(h,ctypes.byref(a),ctypes.byref(b),ctypes.byref(c),ctypes.byref(d))else None
  rss=m.ws if p.GetProcessMemoryInfo(h,ctypes.byref(m),m.cb)else None
  return cpu,rss
 fields=(Path('/proc')/str(pid)/'stat').read_text().rsplit(')',1)[1].split()
 return (int(fields[11])+int(fields[12]))/os.sysconf('SC_CLK_TCK'),int(fields[21])*os.sysconf('SC_PAGE_SIZE')

def host_memory():
 if os.name=='nt':
  k,_,_,_,Memory=windows_api();m=Memory();m.length=ctypes.sizeof(m)
  if not k.GlobalMemoryStatusEx(ctypes.byref(m)):raise OSError('Host memory unavailable')
  return m.total,m.total-m.available
 values={}
 with open('/proc/meminfo')as f:
  for line in f:
   name,rest=line.split(':',1)
   if name in('MemTotal','MemAvailable'):values[name]=int(rest.split()[0])*1024
 return values['MemTotal'],values['MemTotal']-values['MemAvailable']

def system_cpu_counters():
 if os.name=='nt':
  k,_,FT,_,_=windows_api();idle,kernel,user=FT(),FT(),FT()
  if not k.GetSystemTimes(ctypes.byref(idle),ctypes.byref(kernel),ctypes.byref(user)):raise OSError('System load unavailable')
  number=lambda value:(value.hi<<32)|value.lo
  return number(idle),number(kernel)+number(user)
 with open('/proc/stat')as f:values=list(map(int,f.readline(4096).split()[1:9]))
 return values[3]+values[4],sum(values)

class CpuMetrics:
 def __init__(self):
  self.last=-1.;self.cached={};self.cpu_seen={};self.cpu_total=0.;self.points=deque()
 def sample(self,workers,selected,now=None):
  now=time.monotonic()if now is None else now
  pids=tuple(sorted(w.p.pid for w in workers))
  if now-self.last<.25 and self.cached.get('worker_pids')==list(pids):return self.cached
  errors=[];rss=0;memory_known=True;cpu_known=True;seen={}
  try:
   _,own=process_reading(os.getpid());memory_known=own is not None;rss=own or 0
  except (OSError,ValueError,IndexError):memory_known=False
  for w in workers:
   try:
    cpu,mem=process_reading(w.p.pid,getattr(w.p,'_handle',None))
    if cpu is None:cpu_known=False
    else:
     previous=self.cpu_seen.get(w.p.pid)
     if previous is not None and cpu>=previous:self.cpu_total+=cpu-previous
     seen[w.p.pid]=cpu
    if mem is None:memory_known=False
    else:rss+=mem
   except (OSError,ValueError,IndexError):cpu_known=False;memory_known=False
  self.cpu_seen=seen
  self.points.append((now,self.cpu_total))
  while len(self.points)>2 and self.points[1][0]<=now-1.:self.points.popleft()
  span=now-self.points[0][0]
  busy=0. if not workers else max(0.,min(100.,100*(self.cpu_total-self.points[0][1])/span/max(1,selected)))if cpu_known and span>=.25 else None
  if not workers:self.points.clear();self.points.append((now,self.cpu_total))
  try:total,used=host_memory()
  except (OSError,ValueError,KeyError):total=used=None
  if not memory_known:errors.append('Process memory sample unavailable')
  if not cpu_known:errors.append('Worker CPU sample unavailable')
  self.cached=dict(schema='PGN_CPU_TELEMETRY_V1',sample_monotonic_ns=str(int(now*1e9)),worker_pids=list(pids),worker_busy_percent=busy,engine_working_set_bytes=str(rss)if memory_known else None,host_memory_total_bytes=str(total)if total else None,host_memory_used_bytes=str(used)if used is not None else None,working_set_scope='Sum of controller and worker working sets; shared pages may be counted more than once',error='; '.join(errors)or None)
  self.last=now;return self.cached
