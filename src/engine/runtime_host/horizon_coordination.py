"""Confirm an exact retained candidate against the active native Core chain."""
import time,hashlib,re
from horizon_core_rpc import CoreRpcError

def native_header_hash(raw):
 if len(raw)<112:raise ValueError('Whole native header required')
 header=raw[:112];nonce=int.from_bytes(header[80:112],'little')
 # Riecoin's legacy/genesis/scientific identity swaps bits and time for
 # odd nonces or a zero low word. Coordination nonce2 uses wire order.
 # This is the exact CBlockHeader::GetHash relation, not the PoW hash.
 if nonce&1 or nonce&65535==0:header=header[:68]+header[76:80]+header[68:76]+header[80:112]
 return hashlib.sha256(hashlib.sha256(header).digest()).digest()[::-1].hex()

def validate_intent(intent):
 if not isinstance(intent,dict)or not isinstance(intent.get('block'),str)or len(intent['block'])>262144:raise ValueError('Bounded exact coordination candidate required')
 block=intent['block'];identity=intent.get('hash');parent=intent.get('parent')
 if len(block)<226 or len(block)%2 or not re.fullmatch('[0-9a-f]+',block):raise ValueError('Canonical native block bytes required')
 raw=bytes.fromhex(block);digest=native_header_hash(raw)
 if identity!=digest or parent!=raw[4:36][::-1].hex():raise ValueError('Coordination journal hash/parent differs from its exact block')

def active_candidate(rpc,intent):
 validate_intent(intent)
 try:header=rpc.call('getblockheader',[intent['hash'],True])
 except CoreRpcError as error:
  if error.code==-5:return False
  raise
 if not isinstance(header,dict)or header.get('hash')!=intent['hash']:raise ValueError('Coordination header identity mismatch')
 height=header.get('height');confirmations=header.get('confirmations')
 if type(height)is not int or height<0 or type(confirmations)is not int:raise ValueError('Coordination confirmation metadata is invalid')
 return confirmations>0 and rpc.call('getblockhash',[height])==intent['hash']

def confirm_candidate(rpc,intent,*,seconds=30,clock=time.monotonic,sleep=time.sleep):
 # A successful/duplicate submit reply is not proof of active-chain inclusion.
 # On an ambiguous transport error, inspect the exact hash before any retry.
 if active_candidate(rpc,intent):return
 deadline=clock()+seconds;last='No active-chain confirmation'
 while True:
  try:outcome=rpc.call('submitblock',[intent['block']])
  except (OSError,RuntimeError)as error:
   if active_candidate(rpc,intent):return
   raise RuntimeError('Coordination submission outcome unresolved; exact candidate retained')from error
  if active_candidate(rpc,intent):return
  if outcome not in (None,'time-too-new','duplicate','duplicate-inconclusive','inconclusive'):
   raise RuntimeError('Coordination record rejected; exact candidate retained: '+str(outcome)[:200])
  last=str(outcome)
  if clock()>=deadline:raise RuntimeError('Coordination record is not on the active chain; candidate retained: '+last)
  sleep(.25)
