from pathlib import Path
from contextlib import redirect_stdout
from http.server import BaseHTTPRequestHandler,ThreadingHTTPServer
import errno,io,json,os,socket,ssl,sys,tempfile,threading,unittest
from unittest.mock import patch
from urllib.error import HTTPError,URLError

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/protocol'),str(ROOT/'src/engine/shared_composite'),str(ROOT/'tools')]
from horizon_failure_trace import startup_failure_phase
from horizon_core_rpc import CoreRpc,CoreRpcError,CoreRpcUnavailable
import horizon_network_worker as worker

class StartupRecoveryTests(unittest.TestCase):
 def test_typed_availability_only(self):
  transient=[ConnectionRefusedError(errno.ECONNREFUSED,'refused'),ConnectionResetError(errno.ECONNRESET,'reset'),TimeoutError('timed out'),OSError(10061,'refused'),URLError(ConnectionRefusedError(errno.ECONNREFUSED,'refused')),CoreRpcError(-28,'Loading wallet'),CoreRpcUnavailable(503),HTTPError('http://localhost',429,'busy',{},None)]
  for error in transient:
   self.assertEqual(startup_failure_phase(error,'verifying_core'),'Waiting for connection',repr(error))
   self.assertEqual(startup_failure_phase(error,'normal_cycle_ready'),'Needs attention')
 def test_security_storage_and_semantics_remain_terminal(self):
  errors=[HTTPError('http://localhost',401,'refused',{},None),HTTPError('http://localhost',403,'refused',{},None),CoreRpcError(-5,'Bad signature'),ValueError('Wrong genesis'),OSError(errno.ENOSPC,'disk full'),PermissionError(errno.EACCES,'denied'),FileNotFoundError(errno.ENOENT,'missing','kernel.exe'),URLError(ssl.SSLCertVerificationError(1,'certificate refused')),URLError('unknown error')]
  wrapped=ValueError('Invalid scientific evidence');wrapped.__cause__=ConnectionRefusedError('unrelated earlier failure');errors.append(wrapped)
  for error in errors:self.assertEqual(startup_failure_phase(error,'verifying_core'),'Needs attention',repr(error))
 def test_only_missing_cookie_waits(self):
  with tempfile.TemporaryDirectory()as folder:
   cookie=Path(folder)/'.cookie'
   self.assertEqual(startup_failure_phase(FileNotFoundError(errno.ENOENT,'not ready',str(cookie)),'verifying_core',cookie),'Waiting for connection')
   self.assertEqual(startup_failure_phase(FileNotFoundError(errno.ENOENT,'missing',str(cookie.parent/'worker.exe')),'verifying_core',cookie),'Needs attention')
 def exercise_main(self,mode,expected):
  with tempfile.TemporaryDirectory()as folder:
   root=Path(folder);state=root/'state';state.mkdir();cookie=root/'.cookie';cookie.write_bytes(b'synthetic:never-record-this-credential')
   seen=[];genesis='a'*64
   class Handler(BaseHTTPRequestHandler):
    def log_message(self,*args):pass
    def do_POST(self):
     req=json.loads(self.rfile.read(int(self.headers['Content-Length'])));seen.append(req['method']);status=200
     body=dict(id='horizon-worker',result=genesis if req['method']=='getblockhash'else{},error=None)
     if mode in ('busy','rate'):status=503 if mode=='busy' else 429;raw=b'Work queue depth exceeded'
     elif mode=='warmup':status=500;body.update(result=None,error=dict(code=-28,message='Loading wallet'));raw=json.dumps(body).encode()
     elif mode=='auth':status=401;raw=b'Unauthorized'
     elif mode=='wrong-genesis':body['result']='b'*64;raw=json.dumps(body).encode()
     elif mode=='malformed':raw=b'not-json'
     elif mode=='bad-signature':status=500;body.update(result=None,error=dict(code=-5,message='Invalid signature'));raw=json.dumps(body).encode()
     else:raw=json.dumps(body).encode()
     self.send_response(status);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(raw)));self.end_headers();self.wfile.write(raw)
   server=None;thread=None;reserved=None
   if mode=='refused':
    reserved=socket.socket();reserved.bind(('127.0.0.1',0));port=reserved.getsockname()[1]
   else:
    server=ThreadingHTTPServer(('127.0.0.1',0),Handler);port=server.server_port;thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
   class Probe:
    def __init__(self,args):self.args=args
    def run(self):
     (state/'runtime-startup.json').write_text(json.dumps(dict(schema='PGN_CONTROLLER_STARTUP_V1',pid=os.getpid(),phase='verifying_core')))
     rpc=CoreRpc(port,cookie,genesis,'synthetic')
     try:rpc.verify()
     finally:rpc.cancel_all()
   args=['worker','--network','--bundle',str(root),'--state-root',str(state),'--gpu','GPU-SYNTHETIC','--architecture','86','--rpc-port',str(port),'--cookie',str(cookie),'--genesis',genesis,'--wallet','synthetic','--reward','synthetic']
   output=io.StringIO()
   try:
    with patch.object(worker,'NetworkWorker',Probe),patch.object(sys,'argv',args),redirect_stdout(output):exit_code=worker.main()
   finally:
    if reserved:reserved.close()
    if server:server.shutdown();server.server_close();thread.join(timeout=3)
   text=output.getvalue();self.assertNotIn('never-record-this-credential',text)
   if expected is None:
    self.assertEqual(exit_code,0);self.assertEqual(text,'');self.assertEqual(seen,['getblockhash','getpgnstate'])
   else:
    self.assertEqual(exit_code,1);packet=json.loads(text);self.assertEqual(packet['phase'],expected);self.assertEqual(packet['failure']['step'],'verifying_core');self.assertEqual(len(seen),0 if mode=='refused' else 1)
    if expected=='Waiting for connection':self.assertIn('Saved work retained',packet['message'])
 def test_actual_worker_protocol_after_real_core_failures(self):
  for mode in ('refused','busy','rate','warmup'):
   with self.subTest(mode=mode):self.exercise_main(mode,'Waiting for connection')
 def test_actual_worker_protocol_preserves_refusals(self):
  for mode in ('auth','wrong-genesis','malformed','bad-signature'):
   with self.subTest(mode=mode):self.exercise_main(mode,'Needs attention')
 def test_available_core_preflight_works(self):self.exercise_main('ready',None)

if __name__=='__main__':unittest.main()
