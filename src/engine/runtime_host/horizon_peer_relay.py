"""Client-local TLS bootstrap bridge. Native P2P bytes stay opaque and unchanged."""
import argparse,asyncio,json,re,ssl,sys,threading,traceback,ipaddress

READ_LIMIT=64*1024
WRITE_HIGH=256*1024
MAX_EVENT_BYTES=4096

class PeerRelay:
 def __init__(self,host,port,*,listen_port=0,max_connections=4,context=None,connect_address=None,emit=None):
  if not isinstance(host,str)or not re.fullmatch(r'[A-Za-z0-9][A-Za-z0-9.-]{0,252}',host):raise ValueError('Canonical bootstrap DNS name required')
  if type(port)is not int or not 1<=port<=65535 or type(listen_port)is not int or not 0<=listen_port<=65535:raise ValueError('Valid bootstrap and local ports required')
  if type(max_connections)is not int or not 1<=max_connections<=8:raise ValueError('Client-local connection budget must be1..8')
  self.host,self.port,self.listen_port=host,port,listen_port
  self.maximum=max_connections;self.context=context or ssl.create_default_context()
  if self.context.verify_mode!=ssl.CERT_REQUIRED or not self.context.check_hostname:raise ValueError('Certificate and hostname verification are mandatory')
  self.context.minimum_version=max(self.context.minimum_version,ssl.TLSVersion.TLSv1_2)
  self.connect_address=connect_address or host;self.emit=emit or(lambda value:None);self.server=None;self.connections=set();self.closed=False;self.close_task=None;self.sequence=0;self.peak_buffer_bytes=0
 def event(self,event,**fields):
  value=dict(schema='PGN_PEER_RELAY_V1',event=event,**fields)
  if len(json.dumps(value).encode())>MAX_EVENT_BYTES:raise ValueError('Relay diagnostic exceeds its budget')
  self.emit(value)
 async def start(self):
  if self.server is not None:raise RuntimeError('Relay already started')
  self.server=await asyncio.start_server(self.handle,'127.0.0.1',self.listen_port,limit=READ_LIMIT)
  self.local_port=self.server.sockets[0].getsockname()[1]
  self.event('ready',local_host='127.0.0.1',local_port=self.local_port,bootstrap_host=self.host,bootstrap_port=self.port)
  return self.local_port
 async def pipe(self,reader,writer):
  count=0
  while True:
   chunk=await reader.read(READ_LIMIT)
   if not chunk:return count
   writer.write(chunk)
   buffered=writer.transport.get_write_buffer_size();self.peak_buffer_bytes=max(self.peak_buffer_bytes,buffered)
   if buffered>WRITE_HIGH+2*READ_LIMIT:raise BufferError('Relay write buffer exceeded its admitted bound')
   await writer.drain();count+=len(chunk)
 async def close_writer(self,writer):
  if writer is None:return
  writer.close()
  try:await asyncio.wait_for(writer.wait_closed(),2)
  except (OSError,asyncio.TimeoutError):writer.transport.abort()
 async def handle(self,local_reader,local_writer):
  if self.closed or len(self.connections)>=self.maximum:
   self.event('capacity',accepted=False);await self.close_writer(local_writer);return
  task=asyncio.current_task();self.connections.add(task);self.sequence+=1;identity=self.sequence;remote_writer=None;pumps=[]
  try:
   remote_reader,remote_writer=await asyncio.wait_for(asyncio.open_connection(self.connect_address,self.port,ssl=self.context,server_hostname=self.host,ssl_handshake_timeout=10,limit=READ_LIMIT),15)
   for writer in(local_writer,remote_writer):writer.transport.set_write_buffer_limits(high=WRITE_HIGH,low=READ_LIMIT)
   session=remote_writer.get_extra_info('ssl_object')
   if session is None or not session.getpeercert():raise ssl.SSLError('Verified TLS peer certificate required')
   self.event('connected',connection=identity,tls_verified=True,tls_version=session.version())
   pumps=[asyncio.create_task(self.pipe(local_reader,remote_writer)),asyncio.create_task(self.pipe(remote_reader,local_writer))]
   done,pending=await asyncio.wait(pumps,return_when=asyncio.FIRST_COMPLETED)
   for result in done:result.result()
   for result in pending:result.cancel()
   await asyncio.gather(*pumps,return_exceptions=True)
   self.event('disconnected',connection=identity)
  except asyncio.CancelledError:raise
  except Exception as error:self.event('error',connection=identity,type=type(error).__name__,message=str(error)[:240])
  finally:
   for pump in pumps:
    if not pump.done():pump.cancel()
   if pumps:await asyncio.gather(*pumps,return_exceptions=True)
   await self.close_writer(remote_writer);await self.close_writer(local_writer);self.connections.discard(task)
 async def close(self):
  if self.close_task is None:self.close_task=asyncio.create_task(self.finish_close())
  await asyncio.shield(self.close_task)
 async def finish_close(self):
  self.closed=True
  if self.server is not None:self.server.close()
  # Server.wait_closed may include accepted transports. Stop peers before
  # waiting for the listener's complete shutdown, including backpressure.
  tasks=list(self.connections)
  for task in tasks:task.cancel()
  if tasks:await asyncio.gather(*tasks,return_exceptions=True)
  if self.server is not None:await asyncio.wait_for(self.server.wait_closed(),3)
  self.event('closed',remaining_connections=len(self.connections))

async def serve(args,emit):
 address=str(ipaddress.ip_address(args.connect_address))if args.connect_address else None
 relay=PeerRelay(args.host,args.port,listen_port=args.listen_port,max_connections=args.max_connections,connect_address=address,emit=emit)
 stop=asyncio.Event();loop=asyncio.get_running_loop()
 def controls():
  try:
   for line in sys.stdin:
    if len(line)>1024:break
    try:value=json.loads(line)
    except ValueError:break
    if value==dict(action='quit'):break
  finally:loop.call_soon_threadsafe(stop.set)
 threading.Thread(target=controls,daemon=True).start()
 try:await relay.start();await stop.wait()
 finally:await relay.close()

def main(argv=None):
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--host',required=True);parser.add_argument('--port',required=True,type=int)
 parser.add_argument('--listen-port',type=int,default=0);parser.add_argument('--max-connections',type=int,default=4)
 parser.add_argument('--connect-address',help='Optional numeric route for diagnostics; TLS still verifies --host')
 args=parser.parse_args(argv)
 def emit(value):print(json.dumps(value,separators=(',',':')),flush=True)
 try:asyncio.run(serve(args,emit));return 0
 except KeyboardInterrupt:return 0
 except Exception as error:traceback.print_exc(limit=8,file=sys.stderr);emit(dict(schema='PGN_PEER_RELAY_V1',event='error',type=type(error).__name__,message=str(error)[:240]));return 1
if __name__=='__main__':sys.exit(main())
