"""Read-only local NVIDIA discovery. No CUDA job or reservation is started."""
import csv,io,json,math,re,shutil,subprocess,sys
FIELDS=('uuid','name','compute_cap','driver_version','memory.total','memory.used','utilization.gpu','power.draw','power.limit','temperature.gpu','clocks.sm','clocks.mem')
def finite(value):
 try:number=float(value)
 except (ValueError,TypeError):return None
 return number if math.isfinite(number)and number>=0 else None
def compatible_architecture(capability,available=(86,120)):
 if not isinstance(capability,str)or not re.fullmatch(r'[1-9][0-9]*[.][0-9]',capability):return None
 major,minor=map(int,capability.split('.'));device=major*10+minor
 return next((arch for arch in sorted(available,reverse=True)if arch//10==major and arch<=device),None)
def parse_gpu_csv(text,available=(86,120)):
 if len(text.encode())>65536:raise ValueError('Hardware inventory exceeds64KiB')
 devices=[];seen=set()
 for row in csv.reader(io.StringIO(text)):
  if not row:continue
  if len(row)!=len(FIELDS):raise ValueError('Unexpected NVIDIA inventory fields')
  data=dict(zip(FIELDS,(x.strip()for x in row)));identity=data['uuid']
  if not re.fullmatch(r'GPU-[0-9a-fA-F-]{36}',identity)or identity in seen:raise ValueError('Invalid or duplicated GPU identity')
  seen.add(identity);memory=finite(data['memory.total']);used=finite(data['memory.used'])
  devices.append(dict(id=identity,name=data['name'][:128],compute_capability=data['compute_cap'],implementation_architecture=compatible_architecture(data['compute_cap'],available),driver_version=data['driver_version'],memory_total_bytes=str(int(memory*1048576))if memory is not None else None,memory_used_bytes=str(int(used*1048576))if used is not None else None,utilization_percent=finite(data['utilization.gpu']),power_watts=finite(data['power.draw']),power_limit_watts=finite(data['power.limit']),temperature_c=finite(data['temperature.gpu']),core_clock_mhz=finite(data['clocks.sm']),memory_clock_mhz=finite(data['clocks.mem'])))
  if len(devices)>32:raise ValueError('Local GPU inventory exceeds32devices')
 return devices
def probe(program=None):
 executable=program or shutil.which('nvidia-smi')
 if not executable:return dict(schema='PGN_LOCAL_HARDWARE_V1',available=False,reason='NVIDIA driver utility not found',devices=[])
 try:r=subprocess.run([str(executable),'--query-gpu='+','.join(FIELDS),'--format=csv,noheader,nounits'],capture_output=True,text=True,encoding='utf8',errors='replace',timeout=6,**({'creationflags':0x08000000}if sys.platform=='win32'else{}))
 except (OSError,subprocess.TimeoutExpired)as error:return dict(schema='PGN_LOCAL_HARDWARE_V1',available=False,reason=str(error)[:240],devices=[])
 if r.returncode:return dict(schema='PGN_LOCAL_HARDWARE_V1',available=False,reason=r.stderr.strip()[:240]or'NVIDIA hardware query failed',devices=[])
 return dict(schema='PGN_LOCAL_HARDWARE_V1',available=True,devices=parse_gpu_csv(r.stdout))
def main():
 try:print(json.dumps(probe(),separators=(',',':')));return 0
 except Exception as error:print(json.dumps(dict(schema='PGN_LOCAL_HARDWARE_V1',available=False,reason=str(error)[:240],devices=[])));return 1
if __name__=='__main__':sys.exit(main())
