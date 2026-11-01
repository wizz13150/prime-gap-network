"""Network readiness before requesting work; never creates a private fork."""
import re,time

def synchronized(rpc,height,expected_hash):
 if type(height)is not int or height<1 or not isinstance(expected_hash,str)or not re.fullmatch('[0-9a-f]{64}',expected_hash):
  raise ValueError('Explicit testnet bootstrap checkpoint required')
 chain=rpc.call('getblockchaininfo')
 blocks=chain.get('blocks');headers=chain.get('headers')
 if type(blocks)is not int or type(headers)is not int or blocks<height or headers!=blocks or chain.get('initialblockdownload')is not False:return False
 peers=rpc.call('getconnectioncount')
 if type(peers)is not int or peers<1:return False
 announcements=rpc.call('getpeerinfo')
 if not isinstance(announcements,list)or not announcements or len(announcements)>512:return False
 bootstrap_peers=[peer for peer in announcements if isinstance(peer,dict)and peer.get('connection_type')=='manual']
 heights=[peer.get('pgn_announced_height')for peer in bootstrap_peers if type(peer.get('pgn_announced_height'))is int and peer['pgn_announced_height']>=0 and type(peer.get('pgn_syncing'))is bool]
 if not heights or blocks<max(heights)or any(peer.get('pgn_syncing')is not False for peer in bootstrap_peers):return False
 if rpc.call('getblockhash',[height])!=expected_hash:raise ValueError('Connected chain differs from the approved testnet checkpoint')
 return True

def wait_for_network(worker):
 while not worker.quitting.is_set():
  if not worker.running.is_set():worker.message('Mining paused; no work requested','Paused')
  elif synchronized(worker.rpc,worker.args.bootstrap_height,worker.args.bootstrap_hash):return True
  else:worker.message('Synchronizing the testnet before requesting an automatic ticket','Synchronizing')
  worker.quitting.wait(1)
 return False
