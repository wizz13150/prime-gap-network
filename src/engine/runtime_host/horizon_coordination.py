"""Confirm an exact retained candidate against the active native Core chain."""
import time,hashlib,re
from horizon_core_rpc import CoreRpcError

def validate_intent(intent):
 if not isinstance(intent,dict)or not isinstance(intent.get('block'),str)or len(intent['block'])>262144:raise ValueError('Bounded exact coordination candidate required')
 block=intent['block'];identity=intent.get('hash');parent=intent.get('parent')
 if len(block)<226 or len(block)%2 or not re.fullmatch('[0-9a-f]+',block):raise ValueError('Canonical native block bytes required')
 raw=bytes.fromhex(block);digest=hashlib.sha256(hashlib.sha256(raw[:112]).digest()).digest()[::-1].hex()
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
