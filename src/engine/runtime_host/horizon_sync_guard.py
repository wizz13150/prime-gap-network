"""Network readiness before requesting work; never creates a private fork."""
import re,time

def synchronized(rpc,height,expected_hash):
 if type(height)is not int or height<1 or not isinstance(expected_hash,str)or not re.fullmatch('[0-9a-f]{64}',expected_hash):
  raise ValueError('Explicit testnet bootstrap checkpoint required')
 chain=rpc.call('getblockchaininfo')
 blocks=chain.get('blocks');headers=chain.get('headers')
 # Core's IBD heuristic also includes tip age. A quiet native chain may have
 # every validated block and still report IBD after a restart. Admit work only
 # through the explicit checkpoint and complete native-download guards below;
 # never change Core's flag or its validation/chain selection rules.
 if type(blocks)is not int or type(headers)is not int or blocks<height or headers!=blocks or type(chain.get('initialblockdownload'))is not bool:return False
 peers=rpc.call('getconnectioncount')
 if type(peers)is not int or peers<1:return False
 announcements=rpc.call('getpeerinfo')
 if not isinstance(announcements,list)or not announcements or len(announcements)>512:return False
 bootstrap_peers=[peer for peer in announcements if isinstance(peer,dict)and peer.get('connection_type')=='manual']
 # A listening seed can have only inbound connections (desktop clients need
 # no public listener). Accept that topology only after Core itself left IBD;
 # the exact checkpoint and every native download/height check still apply.
 if not bootstrap_peers and chain['initialblockdownload']is False:
  # A new inbound connection has no PGN announcement yet. It is not an
  # authoritative synchronization source and must not disable a seed that
  # already has another initialized, validated inbound peer. Manual bootstrap
  # peers retain the strict all-peer checks below.
  bootstrap_peers=[peer for peer in announcements if isinstance(peer,dict)
                   and peer.get('connection_type')=='inbound'and peer.get('inbound')is True
                   and type(peer.get('pgn_announced_height'))is int and peer['pgn_announced_height']>=0
                   and type(peer.get('pgn_syncing'))is bool]
 if not bootstrap_peers:return False
 if any(type(peer.get('pgn_announced_height'))is not int or peer['pgn_announced_height']<0 or type(peer.get('pgn_syncing'))is not bool for peer in bootstrap_peers):return False
 if blocks<max(peer['pgn_announced_height']for peer in bootstrap_peers)or any(peer['pgn_syncing']for peer in bootstrap_peers):return False
 if rpc.call('getblockhash',[height])!=expected_hash:raise ValueError('Connected chain differs from the approved testnet checkpoint')
 return True

def wait_for_network(worker):
 while not worker.quitting.is_set():
  # Publication of already-computed reports must be able to resume after an
  # application restart even when the user keeps the compute devices OFF.
  # Compute/prefetch lanes still enforce running independently.
  if synchronized(worker.rpc,worker.args.bootstrap_height,worker.args.bootstrap_hash):return True
  if not worker.running.is_set():worker.message('Mining paused; no work requested','Paused')
  else:worker.message('Synchronizing the testnet before requesting an automatic ticket','Synchronizing')
  worker.quitting.wait(1)
 return False
