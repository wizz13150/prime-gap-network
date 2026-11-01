"""Prepare local testnet worker profiles. This cannot allocate or compute work."""
from pathlib import Path
import argparse,json,os,re,sys
from horizon_core_rpc import CoreRpc,CoreRpcError
from horizon_engine_capabilities import qualified_workers
from horizon_hardware_probe import probe

def owned_address(rpc,label):
 try:known=rpc.call('getaddressesbylabel',[label],wallet=True)
 except CoreRpcError as error:
  if error.code!=-11:raise
  known={}
 if not isinstance(known,dict)or len(known)>256:raise ValueError('Bounded wallet address-label result required')
 address=next(iter(sorted(known)))if known else rpc.call('getnewaddress',[label,'bech32'],wallet=True)
 if not isinstance(address,str)or not address:raise ValueError('Wallet returned no mining address')
 info=rpc.call('getaddressinfo',[address],wallet=True)
 if not isinstance(info,dict)or info.get('ismine')is not True or info.get('iswatchonly')is True:raise ValueError('Mining identity is not privately owned by the selected wallet')
 return address

def prepare(bundle,state_root,connection,hardware,*,rpc=None,module_checker=qualified_workers):
 bundle=Path(bundle).resolve(strict=True);state_root=Path(state_root).resolve()
 if not isinstance(connection,dict)or connection.get('schema')!='PGN_CORE_LOCAL_CONNECTION_V1':raise ValueError('Verified local Core context required')
 genesis=connection.get('genesis');port=connection.get('rpc_port');wallet=connection.get('wallet');cookie=connection.get('cookie_file')
 if not isinstance(genesis,str)or not re.fullmatch(r'[0-9a-f]{64}',genesis)or type(port)is not int or not 0<port<65536:raise ValueError('Exact native chain and local RPC port required')
 if not isinstance(wallet,str)or not wallet or len(wallet)>256 or not isinstance(cookie,str)or not Path(cookie).is_absolute():raise ValueError('Explicit owned wallet and authentication path required')
 if hardware.get('schema')!='PGN_LOCAL_HARDWARE_V1':raise ValueError('Local hardware facts required')
 suffix='.exe'if os.name=='nt'else '';program=bundle/'network'/('horizon-engine-host'+suffix)
 if not program.is_file():raise ValueError('Installed local Horizon worker host is unavailable')
 bootstrap_file=bundle/'network'/'bootstrap.json'
 if not bootstrap_file.is_file()or bootstrap_file.stat().st_size>8192:raise ValueError('Installed testnet bootstrap descriptor is unavailable')
 bootstrap=json.loads(bootstrap_file.read_bytes());checkpoint=bootstrap.get('checkpoint',{})
 if bootstrap.get('schema')!='PGN_TESTNET_BOOTSTRAP_V1'or bootstrap.get('genesis')!=genesis or type(checkpoint.get('height'))is not int or checkpoint['height']<1 or not re.fullmatch('[0-9a-f]{64}',str(checkpoint.get('hash',''))):raise ValueError('Approved testnet bootstrap checkpoint required')
 devices=hardware.get('devices')
 if not isinstance(devices,list)or len(devices)>32:raise ValueError('Bounded local hardware inventory required')
 rpc=rpc or CoreRpc(port,cookie,genesis,wallet);rpc.verify()
 loaded=rpc.call('listwallets')
 if not isinstance(loaded,list)or wallet not in loaded:raise ValueError('Selected wallet is not loaded; complete wallet setup')
 profiles=[];unsupported=[];seen=set()
 for device in devices:
  identity=device.get('id');arch=device.get('implementation_architecture')
  if not isinstance(identity,str)or not re.fullmatch(r'GPU-[0-9a-fA-F-]{36}',identity)or identity in seen:raise ValueError('Unique actual GPU identity required')
  seen.add(identity)
  if arch is None:unsupported.append(dict(id=identity,reason='No qualified optimized image for this compute capability'));continue
  if type(arch)is not int:raise ValueError('Qualified implementation architecture required')
  module_checker(bundle,arch)
  address=owned_address(rpc,'Horizon mining '+identity)
  folder=state_root/'gpu'/identity
  network=['--network','--bundle',str(bundle),'--state-root',str(folder),'--gpu',identity,'--architecture',str(arch),'--rpc-port',str(port),'--cookie',cookie,'--genesis',genesis,'--wallet',wallet,'--miner',address,'--reward',address,'--pilot-coordination','--wait-for-network-sync','--bootstrap-height',str(checkpoint['height']),'--bootstrap-hash',checkpoint['hash']]
  profiles.append(dict(schema='PGN_LOCAL_WORKER_STREAM_V1',program=str(program),arguments=['--observe','--state-root',str(folder)],network_arguments=network,gpu_uuid=identity,device_name=device.get('name',identity),compute_capability=device.get('compute_capability'),implementation_architecture=arch,local=True,miner_address=address,reward_address=address,wait_for_network_sync=True))
 audit_address=profiles[0]['miner_address']if profiles else owned_address(rpc,'Horizon audit')
 return dict(audit_identity=dict(address=audit_address,genesis=genesis,wallet=wallet),schema='PGN_LOCAL_RUNTIME_PROFILES_V1',profiles=profiles,unsupported=unsupported,hardware=hardware,mining_started=False,allocation_requested=False)

def main(argv=None):
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--bundle',required=True,type=Path);parser.add_argument('--state-root',required=True,type=Path);args=parser.parse_args(argv)
 line=sys.stdin.readline(32769)
 if len(line)>32768:raise ValueError('Local Core context exceeds32KiB')
 connection=json.loads(line)
 result=prepare(args.bundle,args.state_root,connection,probe())
 print(json.dumps(result,separators=(',',':')));return 0
if __name__=='__main__':sys.exit(main())
