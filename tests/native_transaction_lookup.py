"""Read-only transaction RPC on a new isolated clone; never open live/golden data.

The two index modes run on the same owned clone, wallets and peers disabled.
Only the newly created data directory is removed after a passing qualification.
"""
import argparse
import base64
from decimal import Decimal
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import time
from native_ticket_peer_sync import guard,save,sha,GENESIS


def run(args):
 core=Path(args.core).resolve(strict=True);golden=Path(args.golden_data).resolve(strict=True)
 assert sha(core)==args.expected_sha256 and golden.name=='testnet' and not(golden/'.cookie').exists()
 output=Path(args.output).absolute();assert output.parent==output.parent.resolve(strict=True);output.mkdir(exist_ok=False)
 data=output/'owned-clone';chain=data/'testnet';chain.mkdir(parents=True)
 started=time.monotonic();process=None;inputs=[];calls=[]
 result=dict(PASS=False,core_sha256=sha(core),genesis=GENESIS,source_sha256=sha(Path(__file__)),
             live_nodes_touched=False,UI_controlled=False,GPU_used=False,wallets_disabled=True,peers_disabled=True,
             scope='Native transaction lookup against a cloned historical 121-block chain, with/without transaction index. Not a public network or live-wallet test.')
 intent=dict(writer='root',core=str(core),sha256=sha(core),data=str(data),stage='BEFORE_COPY',rollback='Stop only exact owned child; retain failing clone and logs; remove new clone after PASS only.')
 save(output/'INTENT.json',intent)
 probe=socket.socket();probe.bind(('127.0.0.1',0));port=probe.getsockname()[1];probe.close()

 def rpc(method,params=None):
  assert method in {'getblockhash','getblockcount','getblock','getrawtransaction','getpgnexplorerview','getindexinfo','getconnectioncount','verifychain','stop'}
  if method!='stop' and time.monotonic()-started>120:raise TimeoutError('Native transaction qualification exceeded120s')
  auth=base64.b64encode((chain/'.cookie').read_bytes().strip()).decode();client=http.client.HTTPConnection('127.0.0.1',port,timeout=10)
  try:
   client.request('POST','/',json.dumps(dict(jsonrpc='2.0',id='isolated-transaction',method=method,params=params or [])),{'Authorization':'Basic '+auth,'Content-Type':'application/json'})
   body=client.getresponse().read(262145)
  finally:client.close()
  if len(body)>262144:raise ValueError('Native RPC response exceeds256KiB bound: '+method)
  value=json.loads(body,parse_float=Decimal);calls.append(dict(method=method,params=params or [],error=value.get('error')))
  return value,body

 def value(method,params=None):
  envelope,_=rpc(method,params);assert not envelope.get('error'),envelope.get('error');return envelope['result']

 def stop():
  nonlocal process
  if process and process.poll()is None:
   try:rpc('stop')
   finally:
    try:process.wait(timeout=15)
    except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=10)
  if process:assert process.poll()is not None

 def launch(index):
  nonlocal process
  args=[str(core),'-testnet=1','-datadir='+str(data),'-server=1','-rpcport='+str(port),'-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1',
        '-disablewallet=1','-connect=0','-listen=0','-discover=0','-dnsseed=0','-fixedseeds=0','-listenonion=0','-dbcache=64','-maxmempool=16',
        '-maxconnections=0','-txindex='+str(int(index))]
  with(output/('index-'+str(index)+'.stdout.log')).open('wb')as out,(output/('index-'+str(index)+'.stderr.log')).open('wb')as err:
   process=subprocess.Popen(args,cwd=core.parent,stdout=out,stderr=err,creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
  intent.update(stage='INDEX_'+str(index),owned_PID=process.pid);save(output/'INTENT.json',intent)
  deadline=time.monotonic()+35
  while time.monotonic()<deadline:
   assert process.poll()is None,'Owned fixture Core exited'
   try:
    if value('getblockhash',[0])==GENESIS:break
   except(OSError,ValueError,AssertionError):time.sleep(.1)
  else:raise TimeoutError('Owned fixture startup deadline')
  assert value('getconnectioncount')==0 and value('getblockcount')==121

 try:
  for name in ('blocks','chainstate','chainstate-pgn-request-history'):
   source=golden/name;guard(source)
   for base,dirs,files in os.walk(source):
    for name in files:
     path=Path(base)/name;inputs.append(dict(path=str(path),sha256=sha(path)))
   shutil.copytree(source,chain/source.name)
  launch(False)
  largehash=value('getblockhash',[100]);largeblock=value('getblock',[largehash,1])
  try:rpc('getrawtransaction',[largeblock['tx'][-1],1,largehash])
  except ValueError as error:
   assert 'exceeds256KiB'in str(error);result['large_PGN_operation_exceeds_existing_RPC_bound']=True
  if args.projection:
   projected,body=rpc('getpgnexplorerview',['block',largehash,'',0,25]);assert not projected.get('error');projected=projected['result']
   assert projected['schema']=='pgn-explorer-view-1'and projected['kind']=='block'and projected['hash']==largehash
   assert projected['tx_total']==len(largeblock['tx'])and [x['txid']for x in projected['tx']]==largeblock['tx']
   assert len(body)<=65536 and all('hex'not in x and 'vin'not in x and 'vout'not in x for x in projected['tx'])
   (output/'large-block-projection.json').write_bytes(body)
   txid_large=largeblock['tx'][-1]
   first,body=rpc('getpgnexplorerview',['transaction',txid_large,largehash,0,1]);assert not first.get('error');first=first['result']
   assert first['schema']=='pgn-explorer-view-1'and first['kind']=='transaction'and first['txid']==txid_large and first['blockhash']==largehash and first['in_active_chain']is True
   all_inputs=[];all_outputs=[];maximum=len(body);truncated=0
   for offset in range(max(first['vin_total'],first['vout_total'])):
    page,body=rpc('getpgnexplorerview',['transaction',txid_large,largehash,offset,1]);assert not page.get('error');page=page['result']
    assert page['io_offset']==offset and page['io_limit']==1 and len(page['vin'])<=1 and len(page['vout'])<=1 and page['output_atomic']==first['output_atomic']
    maximum=max(maximum,len(body));all_inputs+=page['vin'];all_outputs+=page['vout']
    for out in page['vout']:
     script=out['scriptPubKey'];assert len(script['hex_preview'])<=192 and 'hex'not in script and 'asm'not in script
     assert script['truncated']==(script['bytes']>96);truncated+=int(script['truncated'])
    (output/('large-transaction-page-'+str(offset)+'.json')).write_bytes(body)
   assert [x['n']for x in all_inputs]==list(range(first['vin_total']))and [x['n']for x in all_outputs]==list(range(first['vout_total']))
   assert sum(Decimal(str(x['value']))*Decimal(10**10)for x in all_outputs)==int(first['output_atomic'])and truncated>0
   assert maximum<=65536
   empty,_=rpc('getpgnexplorerview',['transaction',txid_large,largehash,1000000,25]);assert not empty.get('error')and not empty['result']['vin']and not empty['result']['vout']
   for bad in (['block',largehash,'',-1,25],['block',largehash,'',0,0],['block',largehash,'',0,26],['unknown',largehash,'',0,25]):
    rejected,_=rpc('getpgnexplorerview',bad);assert rejected.get('error')
   result['large_projection']=dict(PASS=True,maximum_transaction_response_bytes=maximum,block_response_bytes=len((output/'large-block-projection.json').read_bytes()),vin_total=len(all_inputs),vout_total=len(all_outputs),truncated_script_count=truncated,output_atomic=first['output_atomic'],all_indices_conserved=True,all_output_amounts_conserved=True,empty_offset=True,invalid_pages_rejected=True)
  # Qualify the bounded ordinary path separately; the oversized-operation
  # failure above remains explicit, rather than silently increasing its cap.
  for lookup_height in range(121,0,-1):
   blockhash=value('getblockhash',[lookup_height]);block=value('getblock',[blockhash,1])
   if block['size']<32768 and block['tx']:break
  else:raise AssertionError('Golden fixture contains no bounded ordinary block')
  txid=block['tx'][0]
  result.update(height=121,lookup_height=lookup_height,blockhash=blockhash,transaction_id=txid,transaction_kind='bounded_native_coinbase',transactions_in_block=len(block['tx']))
  missing,raw=rpc('getrawtransaction',[txid,1]);assert missing['error']['code']==-5 and '-txindex' in missing['error']['message']
  (output/'without-index.json').write_bytes(raw)
  anchored,raw=rpc('getrawtransaction',[txid,1,blockhash]);assert not anchored.get('error');a=anchored['result']
  assert a['in_active_chain']is True and a['blockhash']==blockhash and a['confirmations']==122-lookup_height
  assert a['txid']==txid and a['txid']in block['tx'] and a['vin']and a['vout']and a['hex']
  (output/'anchored-transaction.json').write_bytes(raw)
  result['no_index_block_lookup_PASS']=True;stop();launch(True)
  deadline=time.monotonic()+40
  while time.monotonic()<deadline:
   info=value('getindexinfo',['txindex'])
   if info.get('txindex',{}).get('synced')and info['txindex']['best_block_height']==121:break
   time.sleep(.1)
  else:raise TimeoutError('Isolated transaction index deadline')
  indexed,raw=rpc('getrawtransaction',[txid,1]);assert not indexed.get('error');g=indexed['result']
  for key in ('txid','hex','vin','vout','blockhash','confirmations'):assert g[key]==a[key],key
  (output/'indexed-transaction.json').write_bytes(raw)
  unknown,_=rpc('getrawtransaction',['f'*64,1]);assert unknown['error']['code']==-5 and '-txindex'not in unknown['error']['message']
  wrong,_=rpc('getrawtransaction',[txid,1,value('getblockhash',[lookup_height-1])]);assert wrong['error']['code']==-5
  assert value('verifychain',[4,0])is True and value('getconnectioncount')==0
  result.update(indexed_lookup_PASS=True,unknown_id_rejected=True,wrong_block_rejected=True,verifychain4=True,index_info=info)
  if args.projection:
   indexed,body=rpc('getpgnexplorerview',['transaction',txid_large,'',0,25]);assert not indexed.get('error')and indexed['result']['txid']==txid_large
   assert len(body)<=65536 and len(indexed['result']['vout'])==min(25,first['vout_total'])
   (output/'default-transaction-page.json').write_bytes(body)
   result['large_projection']['default_25_row_response_bytes']=len(body)
   result['large_projection']['indexed_direct_lookup_PASS']=True
  stop();assert all(sha(Path(item['path']))==item['sha256']for item in inputs)
  result.update(PASS=True,golden_unchanged=True,elapsed_seconds=round(time.monotonic()-started,3),RPC_calls=len(calls))
 except Exception as error:
  result['error']=type(error).__name__+': '+str(error)[:1600];raise
 finally:
  stop();result['owned_process_closed']=process is None or process.poll()is not None
  (output/'RPC-CALLS.json').write_text(json.dumps(calls,indent=2)+'\n',encoding='utf8')
  save(output/'GOLDEN-HASHES.json',inputs);save(output/'RESULT.json',result)
  if result['PASS']:
   # Verify exact ownership/no reparse points before removing only this clone.
   assert data.resolve().parent==output.resolve() and data.name=='owned-clone';guard(data)
   for log in ('debug.log',):
    if(chain/log).exists():shutil.copy2(chain/log,output/log)
   shutil.rmtree(data);result['new_clone_removed']=True;save(output/'RESULT.json',result)
  intent['stage']='CLOSED_PASS'if result['PASS']else'CLOSED_FAIL';save(output/'INTENT.json',intent)
 print(json.dumps(result,indent=2))


if __name__=='__main__':
 parser=argparse.ArgumentParser();parser.add_argument('--core',required=True);parser.add_argument('--expected-sha256',required=True)
 parser.add_argument('--golden-data',required=True);parser.add_argument('--output',required=True);parser.add_argument('--projection',action='store_true');run(parser.parse_args())
