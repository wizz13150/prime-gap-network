"""Network readiness before requesting work; never creates a private fork."""
import re,time

def _completed_inbound_header_exchange(peer):
 # Core leaves the PGN height hint absent after an empty HEADERS response.
 # Absence is not a height of zero or evidence of a pending native body. The
 # native handler records a height before queuing bodies for nonempty headers.
 # Require the completed handshake and actual header exchange, never just a
 # TCP connection. This fallback is used only by a fully validated seed below.
 if not isinstance(peer,dict)or peer.get('connection_type')!='inbound'or peer.get('inbound')is not True:return False
 if 'pgn_announced_height'in peer or 'pgn_syncing'in peer:return False
 if type(peer.get('version'))is not int or peer['version']<=0 or peer.get('inflight')!=[]:return False
 sent=peer.get('bytessent_per_msg');received=peer.get('bytesrecv_per_msg')
 if not isinstance(sent,dict)or not isinstance(received,dict):return False
 return all(type(value)is int and value>0 for value in
            (sent.get('verack'),received.get('verack'),received.get('headers')))

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
 if not bootstrap_peers:
  # At equal tips a reconnecting follower sends no new headers. A native seed
  # already outside IBD must not wait forever for a nonempty announcement.
  # This does not admit an IBD node, bypass a manual bootstrap peer, or treat
  # an unverified announced height as accepted chain state.
  if chain['initialblockdownload']is not False or not any(_completed_inbound_header_exchange(peer)for peer in announcements):return False
 else:
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
