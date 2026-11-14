"""Hosted Ubuntu reproduction of the closed, wallet-free hourly fixture."""
from pathlib import Path,PurePosixPath
import hashlib,json,os,runpy,shutil,tarfile,urllib.parse,urllib.request

inputs=json.loads(Path(os.environ['GITHUB_EVENT_PATH']).read_bytes())['inputs']
url=inputs['fixture_url'];digest=inputs['fixture_sha256']
assert len(digest)==64 and all(c in '0123456789abcdef'for c in digest)
parsed=urllib.parse.urlparse(url)
assert parsed.scheme=='https'and parsed.hostname and not parsed.username and not parsed.password
assert parsed.hostname.endswith(('.githubusercontent.com','.blob.core.windows.net'))
print('::add-mask::'+url,flush=True)
root=Path('hourly218-result').resolve();assert not root.exists();root.mkdir()
runtime=Path('runtime').resolve();image=runtime/'pgn-core'
manifest=json.loads((runtime/'PRIVATE-HOURLY-FIXTURE.json').read_bytes())
assert manifest['source']=='4c10bce13676ee492a7b3fa8e973a09f7da3135a'and not manifest['public_activation']
assert manifest['image_sha256']=='79a87cbafaf90a6e5c2dbde95197e947872edab2bf295a3db5c8c93d6b38240b'
def sha(p):
 h=hashlib.sha256()
 with p.open('rb')as f:
  for b in iter(lambda:f.read(1048576),b''):h.update(b)
 return h.hexdigest()
assert sha(image)==manifest['image_sha256'];image.chmod(0o755)
archive=root/'fixture.tar.gz';h=hashlib.sha256();count=0
with urllib.request.urlopen(urllib.request.Request(url,headers={'User-Agent':'PGN-owned-hourly-fixture218'}),timeout=60)as response,archive.open('xb')as out:
 for chunk in iter(lambda:response.read(1048576),b''):
  count+=len(chunk);assert count<=256*1024**2;h.update(chunk);out.write(chunk)
assert h.hexdigest()==digest
with tarfile.open(archive,'r:gz')as t:
 members=t.getmembers();assert len(members)<256 and len({m.name for m in members})==len(members)
 pin=json.load(t.extractfile('FIXTURE.json'));assert pin['public_chain_only']and not pin['wallet_copied']and not pin['cookie_copied']
 assert pin['source']==manifest['source']and pin['height']==24369
 expected={r['name']:r for r in pin['files']};assert {m.name for m in members}==set(expected)|{'FIXTURE.json'}
 assert sum(m.size for m in members)<1024**3
 for m in members:
  assert m.isfile()and ':'not in m.name and '\\'not in m.name
  parts=PurePosixPath(m.name).parts;assert not m.name.startswith('/')and '..'not in parts
  allowed=m.name in('FIXTURE.json','PUBLIC-CHAIN.json','public-chain.jsonl')or len(parts)>=4 and parts[:2]==('node','testnet')and parts[2]in('blocks','chainstate','chainstate-pgn-request-history')
  assert allowed and 'wallet'not in m.name.lower()and '.cookie'not in m.name
  p=root/m.name;p.parent.mkdir(parents=True,exist_ok=True)
  with t.extractfile(m)as f,p.open('xb')as out:shutil.copyfileobj(f,out,1048576)
  if m.name!='FIXTURE.json':assert p.stat().st_size==expected[m.name]['bytes']and sha(p)==expected[m.name]['sha256']
(root/'node/.hourly210-owned').write_bytes(b'private-hourly-node210\n')
(root/'DOWNLOAD-PROOF.json').write_bytes((json.dumps(dict(PASS=True,archive_sha256=digest,bytes=count,source=manifest['source'],image_sha256=manifest['image_sha256'],wallet_copied=False,cookie_copied=False),indent=2)+'\n').encode())
import sys
sys.argv=['hourly_snapshot_driver.py','--runtime',str(runtime),'--export',str(root/'public-chain.jsonl'),'--output',str(root)]
runpy.run_path('tools/hourly_snapshot_driver.py',run_name='__main__')
