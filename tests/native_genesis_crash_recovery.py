"""Exercise fresh native bootstrap crash recovery on a synthetic disconnected node."""
from pathlib import Path
import argparse,base64,hashlib,http.client,json,os,shutil,socket,subprocess,tempfile,time
a=argparse.ArgumentParser();a.add_argument('--core',required=True);a.add_argument('--report',required=True);a.add_argument('--expected-version',required=True);args=a.parse_args()
image=Path(args.core).resolve();assert image.is_file()
root=Path(tempfile.mkdtemp(prefix='pgn-bootstrap-crash-')).resolve();(root/'.test-owned').write_text('native-bootstrap-crash-only')
report=Path(args.report).resolve();report.parent.mkdir(parents=True,exist_ok=True)
with socket.socket()as s:s.bind(('127.0.0.1',0));port=s.getsockname()[1]
process=None;log=None;checks=[];failure=None
def rpc(method,params=None,wallet=False):
 cookie=root/'testnet/.cookie'
 auth=base64.b64encode(cookie.read_bytes().strip()).decode()
 c=http.client.HTTPConnection('127.0.0.1',port,timeout=3)
 try:
  c.request('POST','/wallet/bootstrap-crash'if wallet else '/',json.dumps(dict(jsonrpc='2.0',id='native-bootstrap-crash',method=method,params=params or [])),{'Authorization':'Basic '+auth,'Content-Type':'application/json'})
  response=c.getresponse();body=json.loads(response.read())
  if response.status!=200 or body.get('error'):raise RuntimeError(str(body.get('error')or response.status))
  return body['result']
 finally:c.close()
def start(number):
 global process,log
 log=(root/('launch-'+str(number)+'.log')).open('wb')
 flags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)if os.name=='nt'else 0
 process=subprocess.Popen([str(image),'-testnet','-server=1','-listen=0','-connect=0','-dnsseed=0','-fixedseeds=0','-discover=0','-listenonion=0','-par=1','-dbcache=32','-keypool=2','-rpcthreads=2','-rpcworkqueue=16','-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1',f'-rpcport={port}','-datadir='+str(root)],stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,creationflags=flags)
 deadline=time.monotonic()+45
 while process.poll()is None and time.monotonic()<deadline:
  try:return rpc('getblockchaininfo')
  except Exception:time.sleep(.1)
 log.flush()
 with Path(log.name).open('rb')as f:
  f.seek(max(0,os.fstat(f.fileno()).st_size-4096));tail=f.read(4096).decode(errors='replace')
 raise AssertionError('Native bootstrap failed: '+tail)
def crash():
 assert process and process.poll()is None
 process.kill();process.wait(timeout=10);log.close()
try:
 initial=start(0);assert initial['chain']=='test'and initial['blocks']==0
 assert rpc('getnetworkinfo')['subversion']=='/PGN-Core:'+args.expected_version+'/'
 assert not rpc('getpeerinfo')
 rpc('createwallet',['bootstrap-crash'])
 address=rpc('getnewaddress',wallet=True)
 checks.append('native-profile-identity-and-isolation')
 for n in range(1,4):
  for _ in range(20):assert rpc('getblockchaininfo')['bestblockhash']==initial['bestblockhash']
  crash();info=start(n)
  assert info['blocks']==0 and info['bestblockhash']==initial['bestblockhash']
  if 'bootstrap-crash'not in rpc('listwallets'):rpc('loadwallet',['bootstrap-crash'])
  assert rpc('getaddressinfo',[address],wallet=True)['ismine']
  checks.append('crash-'+str(n)+'-retains-genesis-and-wallet')
 rpc('stop');process.wait(timeout=15);assert process.returncode==0;log.close()
 checks.append('normal-stop-after-recovery')
except BaseException as e:
 failure=repr(e)
finally:
 if process and process.poll()is None:
  try:rpc('stop');process.wait(timeout=15)
  except Exception:
   # This is exclusively our synthetic fixture, never an operator process.
   process.kill();process.wait(timeout=10)
 if log and not log.closed:log.close()
 value=dict(PASS=failure is None,checks=checks,error=failure,image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),scope='Fresh disconnected native genesis and synthetic wallet crash recovery; not general chain/P2P/miner capacity',production_changed=False,all_processes_closed=not process or process.poll()is not None)
 report.write_text(json.dumps(value,indent=2),encoding='utf-8')
 assert root.parent==Path(tempfile.gettempdir()).resolve()and root.name.startswith('pgn-bootstrap-crash-')and(root/'.test-owned').read_text()=='native-bootstrap-crash-only'
 shutil.rmtree(root)
 print(json.dumps(value),flush=True)
if failure:raise SystemExit(1)

