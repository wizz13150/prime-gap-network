"""Replay public blocks through a separately labelled hourly node, then restart.

Only this script's child process and marked synthetic datadir may be controlled.
No wallet from an operator machine is copied, opened, or changed.
"""
from pathlib import Path
import argparse,base64,datetime,hashlib,http.client,json,os,socket,subprocess,time
a=argparse.ArgumentParser();a.add_argument('--runtime',required=True);a.add_argument('--export',required=True);a.add_argument('--output',required=True);args=a.parse_args()
runtime=Path(args.runtime).resolve();source=json.loads((runtime/'PRIVATE-HOURLY-FIXTURE.json').read_bytes());export=Path(args.export).resolve();pin=json.loads((export.parent/'PUBLIC-CHAIN.json').read_bytes());out=Path(args.output).resolve();out.mkdir(parents=True,exist_ok=True)
assert source['public_activation']is False and source['genesis']==pin['genesis']
image=runtime/('pgn-core.exe'if os.name=='nt'else'pgn-core');assert hashlib.sha256(image.read_bytes()).hexdigest()==source['image_sha256']
for f in source['files']:
 p=runtime/f['path'];assert p.parent==runtime and p.stat().st_size==f['bytes']and hashlib.sha256(p.read_bytes()).hexdigest()==f['sha256']
h=hashlib.sha256()
with export.open('rb')as f:
 for b in iter(lambda:f.read(1048576),b''):h.update(b)
assert h.hexdigest()==pin['sha256']and export.stat().st_size==pin['bytes']
data=out/'node';marker=data/'.hourly210-owned'
if data.exists():assert marker.is_file()and marker.read_text()=='private-hourly-node210\n'
else:data.mkdir();marker.write_text('private-hourly-node210\n',encoding='ascii',newline='\n')
with socket.socket()as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
process=None;log=None;connection=None;checks=[];failure=None;phase='initial';bad_height=None;last=None;headers=[];timings={};start_all=time.monotonic()
def save(name,value):
 raw=(json.dumps(value,indent=2)+'\n').encode();temporary=out/(name+'.pending');temporary.write_bytes(raw);temporary.replace(out/name)
def rpc(method,params=None):
 global connection
 cookie=data/'testnet/.cookie';auth=base64.b64encode(cookie.read_bytes().strip()).decode()
 if connection is None:connection=http.client.HTTPConnection('127.0.0.1',port,timeout=120)
 try:
  connection.request('POST','/',json.dumps(dict(jsonrpc='2.0',id='hourly-node210',method=method,params=params or [])),{'Authorization':'Basic '+auth,'Content-Type':'application/json'})
  response=connection.getresponse();v=json.loads(response.read())
  if v.get('error'):raise RuntimeError(str(v['error']))
  if response.status!=200:raise RuntimeError('RPC HTTP '+str(response.status))
  return v['result']
 except BaseException:
  connection.close();connection=None;raise
def start(label):
 global process,log,connection
 if connection:connection.close();connection=None
 log=(out/('node-'+label+'.log')).open('ab');flags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS) if os.name=='nt' else 0
 command=[str(image),'-testnet','-server=1','-listen=0','-connect=0','-dnsseed=0','-fixedseeds=0','-discover=0','-listenonion=0','-assumevalid=0','-par=1','-dbcache=64','-keypool=2','-rpcthreads=2','-rpcworkqueue=16','-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1',f'-rpcport={port}','-datadir='+str(data)]
 begin=time.monotonic();process=subprocess.Popen(command,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,creationflags=flags)
 save('PROCESS.json',dict(writer='root/hourly210-fixture',pid=process.pid,image=str(image),image_sha256=source['image_sha256'],data=str(data),command=command,public_changed=False))
 if os.name=='nt':
  import ctypes
  ctypes.windll.kernel32.SetProcessAffinityMask(int(process._handle),1<<3)
 while process.poll()is None and time.monotonic()-begin<90:
  try:
   info=rpc('getblockchaininfo');assert info['chain']=='test';assert rpc('getblockhash',[0])==pin['genesis'];assert not rpc('getpeerinfo');timings[label+'_startup_seconds']=time.monotonic()-begin;return info
  except (OSError,RuntimeError,ValueError):time.sleep(.1)
 log.flush();raise RuntimeError('Private node failed startup at '+label)
def stop():
 global log,connection
 assert process and process.poll()is None;rpc('stop');process.wait(timeout=45);assert process.returncode==0
 if connection:connection.close();connection=None
 log.close();log=None
try:
 info=start('import');phase='import';begin=time.monotonic();initial=info['blocks'];assert initial==pin['height'];checks.append('native-cold-upgrade-from-Windows-qualified-wallet-free-prefix')
 if initial:assert rpc('getblockhash',[initial])
 with export.open('rb')as f:
  for line in f:
   item=json.loads(line);last=item
   if item['height']<=initial:
    if item['height']==initial:
     if connection:connection.close();connection=None
     assert rpc('getblockhash',[initial])==item['hash']
    continue
   if time.monotonic()-begin>1200:raise TimeoutError('1200-second incremental replay bound; keep validated prefix')
   bad_height=item['height'];v=rpc('submitblock',[item['raw']]);assert v is None,(item['height'],item['hash'],v)
   if item['height']%512==0:
    info=rpc('getblockchaininfo');assert info['blocks']==item['height']and info['bestblockhash']==item['hash']
    progress=dict(phase=phase,height=item['height'],target=pin['height'],elapsed=time.monotonic()-begin);save('PROGRESS.json',progress);print(json.dumps(progress),flush=True)
 assert last and last['height']==pin['height'];info=rpc('getblockchaininfo');assert info['blocks']==pin['height']and info['bestblockhash']==pin['tip'];timings['full_import_seconds']=time.monotonic()-begin;checks.append('full-public-prefix-submitted-and-selected-with-assumevalid-disabled')
 current=pin['tip']
 for _ in range(512):
  header=rpc('getblockheader',[current]);
  if header['proof_type']=='prime_gap':headers.append(header)
  if len(headers)>=2:break
  current=header.get('previousblockhash');
  if not current:break
 assert len(headers)>=2 and any(h['mediantime']>=source['median_time']for h in headers),'Need accepted post-activation Gold and a preceding Gold for rollback'
 assert rpc('getblockhash',[24351])=='51bd0c8197cac0835282d8b3b99d49d2731706e1892c32fe758e049fb626f1fa';checks.append('formerly-rejected-unchanged-Gold24351-admitted-under-canonical-fraction')
 metrics=rpc('getpgnnetworkmetrics',[16,7200,600]);save('METRICS-BEFORE.json',metrics);checks.append('post-activation-Gold-and-preceding-Gold-plus-native-metrics-read')
 stop();phase='cold-full';cold=start('cold-full');assert cold['bestblockhash']==pin['tip']and cold['blocks']==pin['height'];checks.append('cold-full-hourly-startup')
 chosen=headers[1];rpc('invalidateblock',[chosen['hash']]);info=rpc('getblockchaininfo');assert info['blocks']==chosen['height']-1 and info['bestblockhash']==chosen['previousblockhash'];checks.append('native-invalidate-restores-earlier-coins-and-history')
 stop();phase='cold-reorganized';again=start('cold-reorganized');assert again['bestblockhash']==chosen['previousblockhash'];checks.append('cold-hourly-startup-with-invalidated-side-descendants')
 rpc('reconsiderblock',[chosen['hash']]);restored=rpc('getblockchaininfo');assert restored['bestblockhash']==pin['tip']and restored['blocks']==pin['height'];checks.append('native-reconsider-restores-full-branch')
 metrics_after=rpc('getpgnnetworkmetrics',[16,7200,600]);assert metrics_after==metrics,'Native hourly metrics changed after restoring identical chain';checks.append('native-hourly-metrics-identical-after-restore')
 stop();phase='cold-restored';final=start('cold-restored');assert final['bestblockhash']==pin['tip'];checks.append('cold-restored-hourly-startup');stop()
except BaseException as e:
 import traceback
 failure=repr(e);save('FAILURE-TRACE.json',dict(traceback=traceback.format_exc()))
finally:
 if process and process.poll()is None:
  try:stop()
  except BaseException:
   process.kill();process.wait(timeout=15)
 if connection:connection.close()
 if log and not log.closed:log.close()
 logs={}
 if failure:
  for p in out.glob('node-*.log'):
   with p.open('rb')as f:f.seek(max(0,p.stat().st_size-4000));logs[p.name]=f.read(4000).decode(errors='replace')
 result=dict(PASS=failure is None,checks=checks,error=failure,phase=phase,last_submitted_height=bad_height,public_prefix=pin,private_activation=source,scientific_headers=[{k:h[k]for k in('hash','height','mediantime','merit_target_q16')}for h in headers],timings=timings,elapsed_seconds=time.monotonic()-start_all,log_tails=logs,all_owned_processes_closed=not process or process.poll()is not None,public_changed=False,scope='Existing native-validated prefix24350 plus unchanged public suffix24351..24369; native hourly Gold, cold restarts and branch invalidation/restoration; not economic safety, network load or 50k-miner qualification')
 save('RESULT.json',result);print(json.dumps({k:v for k,v in result.items()if k not in('private_activation','public_prefix','log_tails')}),flush=True)
if failure:raise SystemExit(1)
