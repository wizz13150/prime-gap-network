import asyncio,os,pathlib,random,shutil,ssl,subprocess,sys,tempfile,unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_peer_relay import PeerRelay,READ_LIMIT,WRITE_HIGH

class RelayTests(unittest.IsolatedAsyncioTestCase):
 @classmethod
 def setUpClass(cls):
  cls.temp=tempfile.TemporaryDirectory(prefix='pgn-tls-relay-');d=pathlib.Path(cls.temp.name);cls.cert=d/'server.pem';cls.key=d/'server.key';conf=d/'openssl.cnf';conf.write_text('[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=localhost\n')
  exe=shutil.which('openssl');assert exe,'OpenSSL required for closed TLS fixture'
  r=subprocess.run([exe,'req','-x509','-newkey','rsa:2048','-nodes','-days','2','-config',str(conf),'-keyout',str(cls.key),'-out',str(cls.cert),'-addext','subjectAltName=DNS:localhost','-addext','basicConstraints=critical,CA:TRUE'],capture_output=True,timeout=15)
  assert r.returncode==0,r.stderr.decode(errors='replace')[-500:]
 @classmethod
 def tearDownClass(cls):cls.temp.cleanup()
 async def asyncSetUp(self):
  self.servers=[];self.relays=[];self.remote_tasks=set()
 async def asyncTearDown(self):
  for relay in self.relays:await relay.close()
  for server in self.servers:server.close()
  for task in list(self.remote_tasks):task.cancel()
  if self.remote_tasks:await asyncio.gather(*list(self.remote_tasks),return_exceptions=True)
  for server in self.servers:await asyncio.wait_for(server.wait_closed(),3)
 def trusted(self):
  c=ssl.create_default_context(cafile=str(self.cert));return c
 async def tls_server(self,echo=True):
  c=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);c.load_cert_chain(str(self.cert),str(self.key))
  async def handle(reader,writer):
   task=asyncio.current_task();self.remote_tasks.add(task)
   try:
    if not echo:await asyncio.sleep(30)
    else:
     while True:
      chunk=await reader.read(7777)
      if not chunk:break
      writer.write(chunk);await writer.drain()
   except (ConnectionError,asyncio.CancelledError):pass
   finally:
    writer.close()
    try:await asyncio.wait_for(writer.wait_closed(),2)
    except (OSError,asyncio.CancelledError,asyncio.TimeoutError):writer.transport.abort()
    self.remote_tasks.discard(task)
  server=await asyncio.start_server(handle,'127.0.0.1',0,ssl=c,limit=READ_LIMIT);self.servers.append(server);return server.sockets[0].getsockname()[1]
 async def relay(self,port,**kwargs):
  events=[];relay=PeerRelay('localhost',port,connect_address='127.0.0.1',emit=events.append,**kwargs);self.relays.append(relay);local=await relay.start();return relay,local,events
 async def test_binary_bytes_are_unchanged(self):
  port=await self.tls_server();relay,local,events=await self.relay(port,context=self.trusted())
  reader,writer=await asyncio.open_connection('127.0.0.1',local)
  payload=random.Random(13150).randbytes(2*1024*1024)
  async def send():
   for start in range(0,len(payload),READ_LIMIT):writer.write(payload[start:start+READ_LIMIT]);await writer.drain()
  _,answer=await asyncio.wait_for(asyncio.gather(send(),reader.readexactly(len(payload))),10)
  self.assertEqual(answer,payload);self.assertTrue(any(x['event']=='connected'and x['tls_verified']for x in events));self.assertLessEqual(relay.peak_buffer_bytes,WRITE_HIGH+2*READ_LIMIT)
  writer.close();await writer.wait_closed()
 async def test_untrusted_certificate_does_not_forward(self):
  port=await self.tls_server();relay,local,events=await self.relay(port)
  reader,writer=await asyncio.open_connection('127.0.0.1',local);writer.write(b'native-p2p');await writer.drain()
  self.assertEqual(await asyncio.wait_for(reader.read(),5),b'');self.assertFalse(any(x['event']=='connected'for x in events));self.assertTrue(any(x['event']=='error'for x in events));writer.close();await writer.wait_closed()
 async def test_hostname_verification_cannot_be_disabled(self):
  c=self.trusted();c.check_hostname=False
  with self.assertRaises(ValueError):PeerRelay('localhost',443,context=c)
 async def test_wrong_hostname_is_rejected(self):
  port=await self.tls_server();events=[];relay=PeerRelay('wrong.invalid',port,context=self.trusted(),connect_address='127.0.0.1',emit=events.append);self.relays.append(relay);local=await relay.start()
  reader,writer=await asyncio.open_connection('127.0.0.1',local)
  self.assertEqual(await asyncio.wait_for(reader.read(),5),b'');self.assertFalse(any(x['event']=='connected'for x in events));writer.close();await writer.wait_closed()
 async def test_stop_closes_listener_and_peers(self):
  port=await self.tls_server();relay,local,events=await self.relay(port,context=self.trusted());reader,writer=await asyncio.open_connection('127.0.0.1',local);writer.write(b'hello');await writer.drain();self.assertEqual(await reader.readexactly(5),b'hello')
  await asyncio.wait_for(relay.close(),5);self.assertEqual(await reader.read(),b'');self.assertFalse(relay.connections)
  with self.assertRaises(OSError):await asyncio.open_connection('127.0.0.1',local)
  writer.close();await writer.wait_closed()
 async def test_client_local_connection_budget(self):
  port=await self.tls_server();relay,local,events=await self.relay(port,context=self.trusted(),max_connections=1)
  r1,w1=await asyncio.open_connection('127.0.0.1',local);w1.write(b'1');await w1.drain();self.assertEqual(await r1.readexactly(1),b'1')
  r2,w2=await asyncio.open_connection('127.0.0.1',local);self.assertEqual(await r2.read(),b'');self.assertTrue(any(x['event']=='capacity'for x in events))
  w2.close();await w2.wait_closed();w1.close();await w1.wait_closed()
 async def test_backpressure_is_bounded(self):
  port=await self.tls_server(echo=False);relay,local,events=await self.relay(port,context=self.trusted());reader,writer=await asyncio.open_connection('127.0.0.1',local)
  async def flood():
   for _ in range(256):writer.write(b'x'*READ_LIMIT);await writer.drain()
  task=asyncio.create_task(flood());await asyncio.sleep(.3);self.assertLessEqual(relay.peak_buffer_bytes,WRITE_HIGH+2*READ_LIMIT)
  await asyncio.wait_for(relay.close(),5);task.cancel();await asyncio.gather(task,return_exceptions=True);writer.close()
  try:await writer.wait_closed()
  except (OSError,asyncio.TimeoutError):writer.transport.abort()

if __name__=='__main__':unittest.main()
