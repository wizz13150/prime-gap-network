from pathlib import Path
import base64,http.server,json,socket,sys,tempfile,threading,unittest
ROOT=Path(__file__).resolve().parents[1];sys.path.insert(0,str(ROOT/'src/engine/runtime_host'))
from horizon_core_rpc import CoreRpc,CoreRpcError
class Handler(http.server.BaseHTTPRequestHandler):
 protocol_version='HTTP/1.1'
 def log_message(self,*args):pass
 def do_POST(self):
  body=json.loads(self.rfile.read(int(self.headers['Content-Length'])))
  with self.server.guard:
   self.server.observed.append((self.client_address[1],body['method'],self.headers.get('Authorization')))
   if sum(row[1]=='slow'for row in self.server.observed)>=12:self.server.all_entered.set()
  if body['method']=='disconnect_after_mutation':
   self.close_connection=True;self.connection.shutdown(socket.SHUT_RDWR);self.connection.close();return
  if body['method']=='slow':self.server.entered.set();self.server.release.wait(3)
  value=dict(id=body['id'],result=body['method'],error=None);status=200
  if body['method']=='semantic_error':value.update(result=None,error=dict(code=-8,message='Fixture refusal'));status=500
  if body['method']=='wrong_id':value['id']='another-request'
  raw=b'x'*(4*1024*1024+1)if body['method']=='oversized'else json.dumps(value).encode();self.send_response(status);self.send_header('Content-Length',str(len(raw)))
  if body['method']=='server_close':self.send_header('Connection','close');self.close_connection=True
  self.end_headers()
  try:self.wfile.write(raw);self.wfile.flush()
  except(BrokenPipeError,ConnectionResetError,OSError):pass
class Server(http.server.ThreadingHTTPServer):
 daemon_threads=True;request_queue_size=128
 def handle_error(self,*args):
  error=sys.exc_info()[1]
  if not isinstance(error,ConnectionError):raise error
class RpcTransportTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.cookie=Path(self.tmp.name)/'.cookie';self.cookie.write_text('fixture:one');self.server=Server(('127.0.0.1',0),Handler);self.server.guard=threading.Lock();self.server.observed=[];self.server.entered=threading.Event();self.server.all_entered=threading.Event();self.server.release=threading.Event();self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start();self.rpc=CoreRpc(self.server.server_address[1],self.cookie,'a'*64,'')
 def tearDown(self):
  self.server.release.set();self.rpc.cancel_all();self.server.shutdown();self.server.server_close();self.thread.join(2)
 def test_sequential_calls_reuse_actual_http_connection(self):
  for _ in range(4):self.assertEqual(self.rpc.call('read'),'read')
  self.assertEqual(len({x[0]for x in self.server.observed}),1)
 def test_cookie_is_reread_on_reused_connection(self):
  self.rpc.call('read');self.cookie.write_text('fixture:two');self.rpc.call('read')
  rows=self.server.observed;self.assertEqual(rows[0][0],rows[1][0]);self.assertNotEqual(rows[0][2],rows[1][2]);self.assertEqual(rows[1][2],'Basic '+base64.b64encode(b'fixture:two').decode())
 def test_server_close_discards_connection(self):
  self.rpc.call('server_close');self.rpc.call('read');self.assertEqual(len({x[0]for x in self.server.observed}),2)
 def test_rpc_error_can_reuse_a_fully_consumed_http_reply(self):
  with self.assertRaises(CoreRpcError):self.rpc.call('semantic_error')
  self.rpc.call('read');self.assertEqual(len({x[0]for x in self.server.observed}),1)
 def test_wrong_response_identity_is_not_reused(self):
  with self.assertRaises(ValueError):self.rpc.call('wrong_id')
  self.rpc.call('read');self.assertEqual(len({x[0]for x in self.server.observed}),2)
 def test_mutation_is_never_implicitly_replayed_after_lost_response(self):
  with self.assertRaises(Exception):self.rpc.call('disconnect_after_mutation')
  self.assertEqual([x[1]for x in self.server.observed],['disconnect_after_mutation']);self.assertEqual(self.rpc.call('read'),'read')
 def test_cancel_closes_active_and_idle_and_future_calls_remain_possible(self):
  self.rpc.call('read');errors=[]
  def call():
   try:self.rpc.call('slow')
   except Exception as error:errors.append(type(error).__name__)
  thread=threading.Thread(target=call);thread.start();self.assertTrue(self.server.entered.wait(2));self.rpc.cancel_all();self.server.release.set();thread.join(2);self.assertFalse(thread.is_alive());self.assertTrue(errors);self.assertEqual(self.rpc.call('read'),'read')
 def test_idle_connection_budget_under_concurrent_calls(self):
  errors=[]
  def call():
   try:self.rpc.call('slow')
   except Exception as error:errors.append(type(error).__name__)
  threads=[threading.Thread(target=call)for _ in range(12)]
  for thread in threads:thread.start()
  self.assertTrue(self.server.all_entered.wait(2));self.server.release.set()
  for thread in threads:thread.join(2);self.assertFalse(thread.is_alive())
  self.assertFalse(errors);self.assertEqual(len({x[0]for x in self.server.observed}),12);self.assertLessEqual(len(self.rpc._idle),8)
 def test_expired_idle_connection_is_closed(self):
  self.rpc.call('read');self.rpc.maximum_idle_seconds=0;self.rpc.call('read');self.assertEqual(len({x[0]for x in self.server.observed}),2)
 def test_oversized_reply_cannot_leave_a_reusable_partial_response(self):
  with self.assertRaises(ValueError):self.rpc.call('oversized')
  self.rpc.call('read');self.assertEqual(len({x[0]for x in self.server.observed}),2)
 def test_cancel_before_dispatch_sends_no_request(self):
  original=self.rpc._take_connection
  def take():
   value=original();self.rpc.cancel_all();return value
  self.rpc._take_connection=take
  with self.assertRaises(RuntimeError):self.rpc.call('read')
  self.assertFalse(self.server.observed)
 def test_slow_request_is_observable_without_logging_private_payload(self):
  secret='private-argument-not-for-diagnostics';errors=[]
  def slow():
   try:self.rpc.call('slow',[secret])
   except Exception as error:errors.append(error)
  t=threading.Thread(target=slow);t.start();self.assertTrue(self.server.entered.wait(2))
  value=self.rpc.timing_snapshot();self.assertEqual(value['active'][0]['method'],'slow')
  self.assertNotIn(secret,json.dumps(value));self.assertNotIn('fixture:one',json.dumps(value))
  self.server.release.set();t.join(2);self.assertFalse(t.is_alive());self.assertFalse(errors)
  value=self.rpc.timing_snapshot();self.assertEqual(value['active'],[]);self.assertTrue(value['recent'][-1]['returned'])
 def test_rpc_timing_history_is_bounded_and_retains_error_code(self):
  for _ in range(35):self.rpc.call('read')
  with self.assertRaises(CoreRpcError):self.rpc.call('semantic_error')
  self.assertLessEqual(len(self.rpc._recent_calls),32)
  last=self.rpc.timing_snapshot()['recent'][-1];self.assertEqual(last['rpc_error_code'],-8);self.assertFalse(last['returned'])
if __name__=='__main__':unittest.main()
