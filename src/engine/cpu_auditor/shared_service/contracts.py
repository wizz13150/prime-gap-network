"""Pure contracts; no legacy miner/controller imports."""
import hashlib,json
SCHEMA='PGN_EXPLICIT_CPU_AUDIT_REQUEST_V1'
PROBE='PGN_CPU_SHARED68_PROBE_REQUEST_V1'
SOURCE='96C600D3486DED57B400FCD4428E904F6A3743E49F587DC85E93847D9FFDDE13'
WORKER_SHA='a4d9cd031d9ddd2450cb30a09e26ac44e1963372363a71a761c464950304f771'
COVERAGE='SOURCE_PRP_INDEPENDENT_PROOF_PENDING'
def digest(v):return hashlib.sha256(json.dumps(v,sort_keys=True,separators=(',',':')).encode()).hexdigest()
def integer(v):
 if type(v)==int:return v
 if type(v)!=str or not v or len(v)>40 or not v.isascii() or not v.isdecimal() or str(int(v))!=v:raise ValueError('Canonical unsigned decimal integer required')
 return int(v)
def validate(request,probe=False):
 if not isinstance(request,dict):raise ValueError('Object request required')
 if probe:
  if set(request)!={'schema','start','end'} or request['schema']!=PROBE:raise ValueError('Separate bounded probe schema required')
  lo,hi=integer(request['start']),integer(request['end'])
  if not 1<=hi-lo<=20000000000:raise ValueError('Probe must own1..20B positions')
  parts=[dict(index=0,start=str(lo),end=str(hi))]
 else:
  if set(request)!={'schema','ticket_id','ticket_start','ticket_end','partitions'} or request['schema']!=SCHEMA:raise ValueError('Exact audit fields required; expected answers forbidden')
  ticket=request['ticket_id']
  if type(ticket)!=str or len(ticket)!=64 or any(c not in '0123456789abcdef' for c in ticket):raise ValueError('Canonical ticket hex required')
  lo,hi=integer(request['ticket_start']),integer(request['ticket_end'])
  if not 1<<66<=lo<hi<=1<<95 or hi-lo!=200000000000000:raise ValueError('Ticket must span2e14 positions in67..95 domain')
  parts=request['partitions']
  if type(parts)!=list or len(parts)!=5:raise ValueError('Five full assigned partitions required')
  seen=set()
  for p in parts:
   if not isinstance(p,dict) or set(p)!={'index','start','end'}:raise ValueError('Exact partition fields required')
   i=p['index']
   if type(i)!=int or not 0<=i<100 or i in seen:raise ValueError('Distinct partition indices0..99 required')
   seen.add(i)
   if integer(p['start'])!=lo+i*2000000000000 or integer(p['end'])!=lo+(i+1)*2000000000000:raise ValueError('Full partition ownership differs from ticket')
 for p in parts:
  a,b=integer(p['start']),integer(p['end'])
  if not (1<<67)<=a<b<=(1<<68):raise ValueError('Owned68 only; unsupported widths have no scientific fallback')
 return parts
