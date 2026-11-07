"""Current Core clean P2P synchronization against a closed golden native chain.

Two isolated loopback peers, wallets disabled, no scientific/GPU work. The
golden data is copied, never opened in place. Explicit executable/hash/data/new
output arguments prevent accidental use of live node profiles.
"""
import argparse
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import stat
import subprocess
import time

GENESIS='74530feede52b8cda4fa19077747deededef0562844762a43cce462efd29102d'


def sha(path):
 value=hashlib.sha256()
 with path.open('rb')as source:
  for data in iter(lambda:source.read(65536),b''):value.update(data)
 return value.hexdigest()


def guard(path):
 for base,dirs,files in os.walk(path,followlinks=False):
  for name in dirs+files:
   item=(Path(base)/name).lstat()
   if stat.S_ISLNK(item.st_mode)or getattr(item,'st_file_attributes',0)&0x400:raise ValueError('Redirected fixture path')


def save(path,value):
 temporary=path.with_suffix(path.suffix+'.tmp');temporary.write_text(json.dumps(value,indent=2)+'\n',encoding='utf8');temporary.replace(path)


def run(args):
 core=Path(args.core).resolve(strict=True);golden=Path(args.golden_data).resolve(strict=True)
 assert sha(core)==args.expected_sha256 and golden.name=='testnet'and not(golden/'.cookie').exists()
 output=Path(args.output).absolute();assert output.parent==output.parent.resolve(strict=True)
 output.mkdir(exist_ok=False)
 nodes=[];inputs=[];samples=[];started=time.monotonic();tls=None
 result=dict(PASS=False,core_sha256=args.expected_sha256,genesis=GENESIS,live_nodes_touched=False,
             GPU_used=False,UI_controlled=False,wallets_disabled=True,scientific_compute_executed=False,
             test_source_sha256=sha(Path(__file__)),maximum_connections=16,
             scope='Two local native peers; empty destination downloads historical121block/10000request fixture through P2P and validates it. Not10000clients,publicWANorwallet-rescan benchmark.')
 reserved=[]
 for _ in range(4):
  probe=socket.socket();probe.bind(('127.0.0.1',0));reserved.append(probe)
 ports=[item.getsockname()[1]for item in reserved]
 for item in reserved:item.close()
 phase=dict(writer='root',core=str(core),sha256=args.expected_sha256,output=str(output),ports=ports,stage='BEFORE_COPY',rollback='Stop only exact owned processes; remove only newly created clones after PASS')
 save(output/'INTENT.json',phase)

 def rpc(node,method,params=None):
  if method!='stop'and time.monotonic()-started>180:raise TimeoutError('This exact peer qualification exceeded180s')
  auth=base64.b64encode((node['data']/'testnet/.cookie').read_bytes().strip()).decode()
  client=http.client.HTTPConnection('127.0.0.1',node['rpc'],timeout=25)
  try:
   client.request('POST','/',json.dumps(dict(jsonrpc='2.0',id='isolated-p2p',method=method,params=params or[])),{'Authorization':'Basic '+auth,'Content-Type':'application/json'})
   raw=client.getresponse().read(4*1024*1024+1)
  finally:client.close()
  assert len(raw)<=4*1024*1024
  value=json.loads(raw)
  if value.get('error'):raise RuntimeError(value['error'])
  return value['result']

 def launch(label,rpcport,peerport,connect=None):
  data=output/label;data.mkdir(exist_ok=True)
  command=[str(core),'-testnet=1','-datadir='+str(data),'-server=1','-rpcport='+str(rpcport),'-rpcbind=127.0.0.1',
   '-rpcallowip=127.0.0.1','-disablewallet=1','-port='+str(peerport),'-bind=127.0.0.1:'+str(peerport),
   '-listen=1','-discover=0','-dnsseed=0','-fixedseeds=0','-listenonion=0','-dbcache=64','-maxmempool=16',
   '-maxconnections=16','-connect='+('127.0.0.1:'+str(connect)if connect else'0')]
  with(output/(label+'.stdout.log')).open('wb')as out,(output/(label+'.stderr.log')).open('wb')as err:
   process=subprocess.Popen(command,cwd=core.parent,stdout=out,stderr=err,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
  node=dict(label=label,data=data,rpc=rpcport,process=process,began=time.monotonic());nodes.append(node)
  phase.update(stage='STARTED_'+label,owned_PIDs=[item['process'].pid for item in nodes]);save(output/'INTENT.json',phase)
  while time.monotonic()-node['began']<35:
   assert process.poll()is None,'Isolated peer exited during startup'
   try:
    if rpc(node,'getblockhash',[0])==GENESIS:return node
   except(OSError,ValueError,RuntimeError):time.sleep(.1)
  raise TimeoutError('Isolated peer startup deadline')

 def inventory(node):
  page=rpc(node,'getpgnstate',[0,100]);assert page['active_lease_count']==10000 and page['height']==121
  leases=list(page['leases']);tip=page['bestblockhash']
  for offset in range(100,10000,100):
   following=rpc(node,'getpgnstate',[offset,100]);assert following['bestblockhash']==tip;leases.extend(following['leases'])
  assert len(leases)==10000 and len({row['request']for row in leases})==10000
  canonical=json.dumps(sorted(leases,key=lambda row:row['request']),sort_keys=True,separators=(',',':')).encode()
  return dict(tip=tip,height=page['height'],requests=len(leases),sha256=hashlib.sha256(canonical).hexdigest())

 try:
  original_target=output/'source/testnet';original_target.mkdir(parents=True)
  for name in ('blocks','chainstate','chainstate-pgn-request-history'):
   source=golden/name;guard(source)
   for base,dirs,files in os.walk(source):
    for name in files:
     path=Path(base)/name;inputs.append(dict(path=str(path),sha256=sha(path)))
   shutil.copytree(source,original_target/source.name)
  assert all(sha(Path(item['path']))==item['sha256']for item in inputs)
  source=launch('source',ports[0],ports[1]);expected=inventory(source)
  result['source_initialblockdownload']=rpc(source,'getblockchaininfo')['initialblockdownload']
  assert rpc(source,'getconnectioncount')==0
  peer_port=ports[1]
  if args.tls_relay:
   from peer_tls_fixture import TLSFixture
   tls=TLSFixture(output/'tls-fixture',ports[1],Path(args.openssl).resolve(strict=True));peer_port=tls.start()
   result['scope']='Two isolated native peers through unchanged production PeerRelay and local trusted TLS fixture. First download cut after32KiB then native onetry reattach. Not public WAN or wallet-rescan benchmark.'
  before=time.monotonic();destination=launch('destination',ports[2],ports[3],peer_port)
  if tls:
   assert tls.fault.wait(15),'TLS path did not reach deliberate cut'
   interrupted=rpc(destination,'getblockchaininfo')
   assert interrupted['bestblockhash']!=expected['tip'],'Fault happened after complete download'
   result['interrupted_destination_height']=interrupted['blocks']
   deadline=time.monotonic()+10
   while rpc(destination,'getconnectioncount')and time.monotonic()<deadline:time.sleep(.05)
   assert rpc(destination,'getconnectioncount')==0
   resumed=time.monotonic();tls.release()
   rpc(destination,'addnode',['127.0.0.1:'+str(peer_port),'onetry'])
  while time.monotonic()-before<100:
   view=rpc(destination,'getblockchaininfo')
   samples.append(dict(seconds=round(time.monotonic()-before,4),blocks=view['blocks'],headers=view['headers'],tip=view['bestblockhash']))
   if view['bestblockhash']==expected['tip']:break
   time.sleep(.1)
  else:raise TimeoutError('Destination did not reach selected historical tip within100s')
  tip_seconds=time.monotonic()-before
  began=time.monotonic();actual=inventory(destination);ready_seconds=time.monotonic()-before
  assert actual==expected
  assert rpc(source,'getconnectioncount')==rpc(destination,'getconnectioncount')==1
  assert rpc(destination,'verifychain',[4,0])is True
  assert all(sha(Path(item['path']))==item['sha256']for item in inputs)
  peers=rpc(destination,'getpeerinfo')
  if tls:
   assert all(peer['addr']=='127.0.0.1:'+str(peer_port)for peer in peers)
   connected=[event for event in tls.events if event['event']=='connected']
   assert len(connected)>=2 and all(event['tls_verified']for event in connected)
   result['resume_to_target_tip_seconds']=round(before+tip_seconds-resumed,4)
   result['reattach']='Native addnode onetry, matching Horizon eager-attach operation; not automatic retry latency.'
  result.update(PASS=True,source_inventory=expected,destination_inventory=actual,
   destination_process_to_target_tip_seconds=round(tip_seconds,4),
   destination_process_to_all10000requests_seconds=round(ready_seconds,4),
   destination_inventory_query_seconds=round(ready_seconds-tip_seconds,4),
   native_peers=1,full_verifychain4=True,source_golden_unchanged=True,
   destination_initialblockdownload=view['initialblockdownload'],
   historical_tip_time=rpc(destination,'getblockheader',[expected['tip']])['time'],
   peer=[{key:peer.get(key)for key in ('addr','inbound','version','subver','synced_headers','synced_blocks','connection_type')}for peer in peers])
 except BaseException as error:
  result['error']=repr(error)
  result['failure_nodes']=[]
  for node in nodes:
   if node['process'].poll()is None:
    try:result['failure_nodes'].append(dict(label=node['label'],blockchain=rpc(node,'getblockchaininfo'),network=rpc(node,'getnetworkinfo'),peers=rpc(node,'getpeerinfo')))
    except BaseException as diagnostic:result['failure_nodes'].append(dict(label=node['label'],diagnostic_error=repr(diagnostic)))
 finally:
  for node in reversed(nodes):
   process=node['process']
   if process.poll()is None:
    try:rpc(node,'stop');process.wait(timeout=30)
    except BaseException as error:
     result.update(PASS=False,cleanup_error=repr(error));process.terminate();process.wait(timeout=10)
  result['clean_stops']=all(node['process'].poll()is not None for node in nodes)
  if tls:
   try:
    result['TLS']=tls.close()
    assert not result['TLS']['server_errors']
   except BaseException as error:result.update(PASS=False,tls_cleanup_error=repr(error))
  if result['PASS']:
   for label in ('source','destination'):
    path=output/label;assert path.resolve().parent==output.resolve();guard(path);shutil.rmtree(path)
   result['owned_clones_removed']=True
   phase.update(stage='PASS_AND_STOPPED',owned_PIDs=[]);save(output/'INTENT.json',phase)
  result['seconds']=round(time.monotonic()-started,3)
  save(output/'SYNC-SAMPLES.json',samples);save(output/'GOLDEN-SOURCE-FINGERPRINTS.json',inputs);save(output/'RESULT.json',result)
 return result


if __name__=='__main__':
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--core',required=True);parser.add_argument('--expected-sha256',required=True)
 parser.add_argument('--golden-data',required=True);parser.add_argument('--output',required=True)
 parser.add_argument('--tls-relay',action='store_true',help='Use production TLS client and deliberately interrupt first download')
 parser.add_argument('--openssl',default='C:/Gapcoin/toolchain/msys64/ucrt64/bin/openssl.exe')
 result=run(parser.parse_args());print(json.dumps(result,sort_keys=True));raise SystemExit(0 if result['PASS']else 1)
