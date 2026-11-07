"""Export bounded status captures; never reads a wallet, work folder or key."""
import io,json,re,sys,zipfile,math
MAX_INPUT=2*1024*1024
MAX_SAMPLES=1800
FORBIDDEN=('cookie','password','secret','private','signature','nonce','counts','answers','report','scopes','best','recent','wif','authorization','seed','token','credential','api_key','apikey')

def clean(value,depth=0):
 if depth>12:raise ValueError('Diagnostic nesting exceeds its bound')
 if isinstance(value,dict):
  if len(value)>128:raise ValueError('Diagnostic object exceeds its bound')
  result={}
  for key,item in value.items():
   if not isinstance(key,str)or len(key)>80 or any(word in key.lower()for word in FORBIDDEN):raise ValueError('Private or unsupported diagnostic field refused')
   result[key]=clean(item,depth+1)
  return result
 if isinstance(value,list):
  if len(value)>(MAX_SAMPLES if depth==1 else 256):raise ValueError('Diagnostic list exceeds its bound')
  return[clean(item,depth+1)for item in value]
 if isinstance(value,str):
  if len(value)>2048:raise ValueError('Diagnostic text exceeds its bound')
  if re.search(r'(?i)(?<![a-z0-9])prv[0-9a-f]{64}(?![0-9a-f])',value):raise ValueError('Native private key in diagnostic text refused')
  value=re.sub(r'(?i)[a-z]:[\\/]Users[\\/][^\\/\s]+',r'<user>',value)
  value=re.sub(r'/(?:home|Users)/[^/\s]+','<user>',value)
  value=re.sub(r'(?i)(?:authorization|cookie|password|secret|private.key)\s*[:=][^\r\n]*','<redacted>',value)
  return value
 if value is None or type(value)in(bool,int):return value
 if type(value)is float and math.isfinite(value):return value
 raise ValueError('Unsupported diagnostic value')

def export(raw):
 if not isinstance(raw,bytes)or len(raw)>MAX_INPUT:raise ValueError('Diagnostic input exceeds 2 MiB')
 value=json.loads(raw)
 if not isinstance(value,dict):raise ValueError('A diagnostic capture object is required')
 samples=value.get('samples')
 if value.get('schema')!='PGN_DIAGNOSTIC_CAPTURE_V1'or not isinstance(samples,list)or not 1<=len(samples)<=MAX_SAMPLES:raise ValueError('A bounded nonempty diagnostic capture is required')
 safe=clean(value);encoded=json.dumps(safe,ensure_ascii=False,allow_nan=False,separators=(',',':')).encode('utf8')
 if len(encoded)>MAX_INPUT:raise ValueError('Sanitized diagnostic capture exceeds its bound')
 stream=io.BytesIO()
 with zipfile.ZipFile(stream,'w',zipfile.ZIP_DEFLATED,compresslevel=6)as archive:
  archive.writestr('diagnostics.json',encoded)
  archive.writestr('README.txt','PGN Horizon status capture\nApplication/network/device status only. No wallet files, keys, credentials, signatures, audit answers or gap tables are included.\nA local calculation is separate from accepted network work.\n')
 result=stream.getvalue()
 if len(result)>MAX_INPUT:raise ValueError('Compressed capture exceeds its bound')
 return result

def main():
 raw=sys.stdin.buffer.read(MAX_INPUT+1)
 sys.stdout.buffer.write(export(raw));sys.stdout.buffer.flush();return 0
if __name__=='__main__':sys.exit(main())
