"""Prepare local testnet worker profiles. This cannot allocate or compute work."""
from pathlib import Path
import argparse,hashlib,json,os,re,sys
from horizon_core_rpc import CoreRpc,CoreRpcError
from horizon_engine_capabilities import qualified_workers
from horizon_scientific_bundle import DESCRIPTOR,qualified_bundle_workers,UnsupportedScientificArchitecture
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

def registered_wallet_address(rpc,genesis):
 """Lookup local wallet candidates, never scan the whole participant registry.

 The authority key is deliberately excluded. A device must reuse an ordinary
 owned mining identity; discovering one never grants a network permission.
 """
 labels=rpc.call('listlabels',wallet=True)
 if type(labels)is not list or len(labels)>4096 or any(type(s)is not str or len(s)>256 for s in labels):raise ValueError('Bounded local wallet labels required')
 seen=set()
 for label in sorted(set(labels),key=lambda s:(not s.startswith('Horizon'),s)):
  try:addresses=rpc.call('getaddressesbylabel',[label],wallet=True)
  except CoreRpcError as error:
   if error.code==-11:continue
   raise
  if type(addresses)is not dict or len(addresses)>8192:raise ValueError('Bounded wallet address candidates required')
  for address in sorted(addresses):
   if address in seen:continue
   seen.add(address)
   if len(seen)>8192:raise ValueError('Select a mining identity for this large wallet')
   page=rpc.call('getpgnparticipants',[0,1,address])
   if type(page)is not dict or page.get('genesis')!=genesis or type(page.get('participants'))is not list or len(page['participants'])>1:raise ValueError('Participant lookup differs from the local chain')
   if not page['participants']:continue
   member=page['participants'][0]
   if member.get('may_audit')is not True or member.get('signing_script')==page.get('authority_script'):continue
   info=rpc.call('getaddressinfo',[address],wallet=True)
   if info.get('ismine')is True and info.get('iswatchonly')is not True and info.get('scriptPubKey')==member.get('signing_script'):return address
 return None

def prepare(bundle,state_root,connection,hardware,*,rpc=None,module_checker=qualified_workers,cpu_only=False):
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
 if type(cpu_only)is not bool:raise ValueError('Explicit CPU-only preparation choice required')
 registered=registered_wallet_address(rpc,genesis)
 if cpu_only:
  address=registered or owned_address(rpc,'Horizon normal CPU mining')
  # An earlier unregistered assignment stays under its original identity.
  identity=hashlib.sha256((genesis+'\0'+wallet+'\0'+address).encode()).hexdigest()
  network=['--network','--cpu-only','--bundle',str(bundle),'--state-root',str(state_root/'cpu'/identity),
   '--rpc-port',str(port),'--cookie',cookie,'--genesis',genesis,'--wallet',wallet,'--miner',address,'--reward',address,
   '--pilot-coordination','--wait-for-network-sync','--bootstrap-height',str(checkpoint['height']),'--bootstrap-hash',checkpoint['hash']]
  profile=dict(schema='PGN_LOCAL_CPU_NORMAL_PROFILE_V1',program=str(program),network_arguments=network,local=True,registered_when_prepared=registered is not None)
  return dict(schema='PGN_LOCAL_RUNTIME_PROFILES_V1',cpu_profile=profile,profiles=[],unsupported=[],
   mining_started=False,allocation_requested=False,audit_identity=dict(address=address,genesis=genesis,wallet=wallet))
 for device in devices:
  identity=device.get('id');arch=device.get('implementation_architecture')
  if not isinstance(identity,str)or not re.fullmatch(r'GPU-[0-9a-fA-F-]{36}',identity)or identity in seen:raise ValueError('Unique actual GPU identity required')
  seen.add(identity)
  if arch is None:unsupported.append(dict(id=identity,reason='No qualified optimized image for this compute capability'));continue
  if type(arch)is not int:raise ValueError('Qualified implementation architecture required')
  checker=qualified_bundle_workers if module_checker is qualified_workers and(bundle/DESCRIPTOR).exists()else module_checker
  try:checker(bundle,arch)
  except UnsupportedScientificArchitecture as error:
   unsupported.append(dict(id=identity,reason=str(error)));continue
  address=registered or owned_address(rpc,'Horizon mining '+identity)
  # A network reset or wallet switch must never reopen an older ticket under
  # a different signer/genesis. Existing folders are retained, never retagged.
  binding=hashlib.sha256((genesis+'\0'+wallet+'\0'+address+'\0'+identity).encode()).hexdigest()
  folder=state_root/'gpu'/identity/binding
  network=['--network','--bundle',str(bundle),'--state-root',str(folder),'--gpu',identity,'--architecture',str(arch),'--rpc-port',str(port),'--cookie',cookie,'--genesis',genesis,'--wallet',wallet,'--miner',address,'--reward',address,'--pilot-coordination','--wait-for-network-sync','--bootstrap-height',str(checkpoint['height']),'--bootstrap-hash',checkpoint['hash']]
  if(bundle/DESCRIPTOR).exists():network.append('--require-scientific-source')
  profiles.append(dict(schema='PGN_LOCAL_WORKER_STREAM_V1',program=str(program),arguments=['--observe','--state-root',str(folder)],network_arguments=network,gpu_uuid=identity,device_name=device.get('name',identity),compute_capability=device.get('compute_capability'),implementation_architecture=arch,local=True,miner_address=address,reward_address=address,wait_for_network_sync=True))
 audit_address=profiles[0]['miner_address']if profiles else owned_address(rpc,'Horizon audit')
 return dict(audit_identity=dict(address=audit_address,genesis=genesis,wallet=wallet),schema='PGN_LOCAL_RUNTIME_PROFILES_V1',profiles=profiles,unsupported=unsupported,hardware=hardware,mining_started=False,allocation_requested=False)

def main(argv=None):
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--bundle',required=True,type=Path);parser.add_argument('--state-root',required=True,type=Path);parser.add_argument('--cpu-only',action='store_true');args=parser.parse_args(argv)
 line=sys.stdin.readline(32769)
 if len(line)>32768:raise ValueError('Local Core context exceeds32KiB')
 connection=json.loads(line)
 hardware=dict(schema='PGN_LOCAL_HARDWARE_V1',devices=[])if args.cpu_only else probe()
 result=prepare(args.bundle,args.state_root,connection,hardware,cpu_only=args.cpu_only)
 print(json.dumps(result,separators=(',',':')));return 0
if __name__=='__main__':sys.exit(main())
