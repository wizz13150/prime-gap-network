"""Isolated TLS server fixture around the unchanged production PeerRelay client.

The first server-to-client stream is cut after32KiB, then admission is held
until the test explicitly releases it. No system certificate store is changed.
"""
import asyncio
import concurrent.futures
from pathlib import Path
import ssl
import subprocess
import sys
import threading

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_peer_relay import PeerRelay,READ_LIMIT,WRITE_HIGH


class TLSFixture:
 def __init__(self,directory,upstream,openssl):
  self.directory=directory;self.upstream=upstream;self.openssl=openssl
  self.loop=asyncio.new_event_loop();self.thread=None;self.events=[];self.server_tasks=set()
  self.fault=threading.Event();self.released=False;self.sent=0;self.cutoff=32768
  self.relay=None;self.server=None;self.server_errors=[]

 def start(self):
  self.directory.mkdir(exist_ok=False)
  config=self.directory/'openssl.cnf';config.write_text('[req]\ndistinguished_name=dn\nprompt=no\n[dn]\nCN=localhost\n',encoding='ascii')
  self.cert=self.directory/'server.pem';self.key=self.directory/'server.key'
  answer=subprocess.run([str(self.openssl),'req','-x509','-newkey','rsa:2048','-nodes','-days','2','-config',str(config),'-keyout',str(self.key),'-out',str(self.cert),'-addext','subjectAltName=DNS:localhost','-addext','basicConstraints=critical,CA:TRUE'],capture_output=True,timeout=15)
  if answer.returncode:raise RuntimeError(answer.stderr.decode(errors='replace')[-500:])
  self.thread=threading.Thread(target=self.loop.run_forever,name='isolated-tls-fixture',daemon=True);self.thread.start()
  return asyncio.run_coroutine_threadsafe(self._start(),self.loop).result(15)

 async def _start(self):
  server_context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);server_context.load_cert_chain(str(self.cert),str(self.key))
  self.server=await asyncio.start_server(self.accept,'127.0.0.1',0,ssl=server_context,limit=READ_LIMIT)
  self.tls_port=self.server.sockets[0].getsockname()[1]
  self.relay=PeerRelay('localhost',self.tls_port,context=ssl.create_default_context(cafile=str(self.cert)),connect_address='127.0.0.1',emit=self.events.append)
  self.local_port=await self.relay.start();return self.local_port

 async def accept(self,reader,writer):
  task=asyncio.current_task();self.server_tasks.add(task);remote=None;pumps=[]
  try:
   if self.fault.is_set()and not self.released:return
   other,remote=await asyncio.open_connection('127.0.0.1',self.upstream,limit=READ_LIMIT)
   for stream in(writer,remote):stream.transport.set_write_buffer_limits(high=WRITE_HIGH,low=READ_LIMIT)
   async def upstream_bytes():
    while True:
     chunk=await other.read(READ_LIMIT)
     if not chunk:return
     if not self.fault.is_set():
      chunk=chunk[:self.cutoff-self.sent]
      writer.write(chunk);await writer.drain();self.sent+=len(chunk)
      if self.sent==self.cutoff:
       self.fault.set();return
     else:writer.write(chunk);await writer.drain()
   pumps=[asyncio.create_task(self.relay.pipe(reader,remote)),asyncio.create_task(upstream_bytes())]
   done,pending=await asyncio.wait(pumps,return_when=asyncio.FIRST_COMPLETED)
   for result in done:result.result()
  except(ConnectionError,asyncio.CancelledError):pass
  except Exception as error:self.server_errors.append(repr(error))
  finally:
   for pump in pumps:pump.cancel()
   if pumps:await asyncio.gather(*pumps,return_exceptions=True)
   await self.relay.close_writer(remote);await self.relay.close_writer(writer);self.server_tasks.discard(task)

 def release(self):
  async def apply():self.released=True
  asyncio.run_coroutine_threadsafe(apply(),self.loop).result(3)

 def close(self):
  async def shutdown():
   if self.server:self.server.close()
   if self.relay:await self.relay.close()
   tasks=list(self.server_tasks)
   for task in tasks:task.cancel()
   if tasks:await asyncio.gather(*tasks,return_exceptions=True)
   if self.server:await asyncio.wait_for(self.server.wait_closed(),3)
  if self.thread:
   try:asyncio.run_coroutine_threadsafe(shutdown(),self.loop).result(12)
   finally:
    self.loop.call_soon_threadsafe(self.loop.stop);self.thread.join(3)
    if self.thread.is_alive():raise RuntimeError('Isolated TLS loop failed to stop')
    self.loop.close()
  if self.key.exists():self.key.unlink()
  return dict(production_client_sha256=__import__('hashlib').sha256(Path(sys.modules[PeerRelay.__module__].__file__).read_bytes()).hexdigest(),
              certificate_and_hostname_verified=True,cutoff_server_to_client_bytes=self.sent,
              first_connection_interrupted=self.fault.is_set(),admission_released=self.released,
              events=self.events,server_errors=self.server_errors,
              peak_client_write_buffer_bytes=self.relay.peak_buffer_bytes if self.relay else None,
              listeners_and_thread_stopped=True,ephemeral_private_key_removed=not self.key.exists())
