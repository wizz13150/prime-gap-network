"""Native PGN VERSION delivery latency must not become clock-based eviction."""
from pathlib import Path
import argparse,base64,hashlib,http.client,json,os,secrets,shutil,socket,struct,subprocess,tempfile,time

def main():
 p=argparse.ArgumentParser();p.add_argument('--core',required=True);p.add_argument('--report',required=True);p.add_argument('--expected-version',required=True);a=p.parse_args()
 image=Path(a.core).resolve();assert image.is_file();root=Path(tempfile.mkdtemp(prefix='pgn-peer-clock-')).resolve();(root/'.test-owned').write_text('native-clock-transport')
 report=Path(a.report).resolve();report.parent.mkdir(exist_ok=True,parents=True);process=None;log=None;failure=None;rows=[]
 def port():
  with socket.socket()as s:s.bind(('127.0.0.1',0));return s.getsockname()[1]
 rpc_port,p2p_port=port(),port()
 def rpc(method):
  auth=base64.b64encode((root/'testnet/.cookie').read_bytes().strip()).decode();c=http.client.HTTPConnection('127.0.0.1',rpc_port,timeout=3)
  try:
   c.request('POST','/',json.dumps(dict(jsonrpc='2.0',id='native-clock-latency',method=method,params=[])),{'Content-Type':'application/json','Authorization':'Basic '+auth});r=c.getresponse();body=json.loads(r.read());assert r.status==200 and not body.get('error');return body['result']
  finally:c.close()
 def frame(s,command,body):
  magic=bytes.fromhex('d7544e32');s.sendall(magic+command.encode().ljust(12,b'\0')+struct.pack('<I',len(body))+hashlib.sha256(hashlib.sha256(body).digest()).digest()[:4]+body)
 def read(s,n):
  out=b''
  while len(out)<n:
   b=s.recv(n-len(out));assert b,'Native PGN disconnected a valid delayed VERSION';out+=b
  return out
 try:
  flags=(subprocess.CREATE_NO_WINDOW|subprocess.BELOW_NORMAL_PRIORITY_CLASS)if os.name=='nt'else 0;log=(root/'process.log').open('wb')
  args=[str(image),'-testnet','-server=1','-listen=1','-bind=127.0.0.1',f'-port={p2p_port}',f'-rpcport={rpc_port}','-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1','-connect=0','-dnsseed=0','-fixedseeds=0','-discover=0','-listenonion=0','-par=1','-dbcache=32','-keypool=2','-datadir='+str(root)]
  process=subprocess.Popen(args,stdin=subprocess.DEVNULL,stdout=log,stderr=subprocess.STDOUT,creationflags=flags)
  deadline=time.monotonic()+45
  while time.monotonic()<deadline:
   assert process.poll()is None
   try:initial=rpc('getblockchaininfo');break
   except Exception:time.sleep(.1)
  else:raise AssertionError('Private native node did not start')
  info=rpc('getnetworkinfo');assert info['subversion']=='/PGN-Core:'+a.expected_version+'/'and initial['blocks']==0
  for delay,sample in ((0,None),(8,None),(0,-2**63),(0,2**63-1)):
   with socket.create_connection(('127.0.0.1',p2p_port),timeout=4)as s:
    s.settimeout(5);stamp=int(time.time())if sample is None else sample
    address=struct.pack('<Q',0)+b'\0'*10+b'\xff\xff\x7f\0\0\1'+b'\0\0';agent=b'/PGN clock regression/'
    body=struct.pack('<iQq',info['protocolversion'],0,stamp)+address+address+secrets.token_bytes(8)+bytes([len(agent)])+agent+struct.pack('<i?',0,False)
    if delay:time.sleep(delay)
    frame(s,'version',body);seen=set();nonce=secrets.token_bytes(8);ping_sent=False;deadline=time.monotonic()+8
    while time.monotonic()<deadline:
     header=read(s,24);assert header[:4]==bytes.fromhex('d7544e32');size=struct.unpack('<I',header[16:20])[0];assert size<4*1024*1024;payload=read(s,size)
     assert hashlib.sha256(hashlib.sha256(payload).digest()).digest()[:4]==header[20:24];command=header[4:16].split(b'\0')[0].decode();seen.add(command)
     if command=='version':frame(s,'verack',b'')
     if {'version','verack'}<=seen and not ping_sent:frame(s,'ping',nonce);ping_sent=True
     if command=='ping':frame(s,'pong',payload)
     if command=='pong'and payload==nonce:break
    else:raise AssertionError('Native peer did not answer ping after VERSION')
    assert command=='pong'and payload==nonce
    current=rpc('getblockchaininfo');assert current['bestblockhash']==initial['bestblockhash']and current['blocks']==0
    # These inbound samples cannot adjust our system clock or the outbound median.
    assert rpc('getnetworkinfo')['timeoffset']==0
    rows.append(dict(delay_seconds=delay,extreme_timestamp=sample,handshake_and_ping=True,chain_unchanged=True))
  rpc('stop');process.wait(timeout=15);assert process.returncode==0
 except BaseException as e:failure=repr(e)
 finally:
  if process and process.poll()is None:
   try:rpc('stop');process.wait(timeout=15)
   except Exception:process.kill();process.wait(timeout=10)
  if log:log.close()
  v=dict(PASS=failure is None,checks=rows,error=failure,image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),all_processes_closed=not process or process.poll()is not None,production_changed=False,scope='Real localhost native PGN handshake, delayed delivery and extreme untrusted VERSION times; no consensus or wall-clock modification')
  report.write_text(json.dumps(v,indent=2),encoding='utf-8');assert root.parent==Path(tempfile.gettempdir()).resolve()and(root/'.test-owned').read_text()=='native-clock-transport';shutil.rmtree(root);print(json.dumps(v),flush=True)
 if failure:raise SystemExit(1)

if __name__=='__main__':main()
