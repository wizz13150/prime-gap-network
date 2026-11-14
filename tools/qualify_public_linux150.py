from pathlib import Path
import hashlib,json,subprocess,tarfile,time
P=Path.cwd();archive=P/'portable.tar.gz'
assert hashlib.sha256(archive.read_bytes()).hexdigest()=='7943b348c6db577c780e0026f2291b3cd90e48d37d4e64d7430fd144c2cb40ee'
with tarfile.open(archive)as z:z.extractall(P/'isolated',filter='data')
files=list((P/'isolated').rglob('UpdateRuntime/horizon-updater'));assert len(files)==1;helper=files[0]
assert hashlib.sha256(helper.read_bytes()).hexdigest()=='5a66939f795f57c8c70cc0ca505d472f7513a88a9198af5389351d4ac7e9d7ee'
helper.chmod(0o700);root=P/'prepare-test';root.mkdir();data=root/'data';data.mkdir();sentinel=data/'wallet.dat';before=b'isolated-not-a-real-wallet\n';sentinel.write_bytes(before)
context=dict(schema='PGN_COMPONENT_STAGE_CONTEXT_V1',network='testnet',genesis_hash='0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d',platform='linux-x86_64',compatibility_set='pgn.epoch2.2026.11',minimum_sequence=1,installed=[],protected_roots=[str(data)])
request=dict(schema='PGN_UPDATE_REQUEST_V1',action='prepare',context=context,cache=str(root/'cache'),selected=['ui','core','gpu','cpu'])
start=time.monotonic();x=subprocess.run([str(helper)],input=json.dumps(request).encode(),capture_output=True,timeout=240)
assert x.returncode==0,x.stderr[-2000:].decode(errors='replace')
v=json.loads(x.stdout);assert v['publisher_authenticated']and v['archive_sha256']=='4315311dc7af7b64f91ad38ffd69aa69774523ec1cc95894ca61cb7feae51f31',v
assert sentinel.read_bytes()==before
(P/'PUBLIC138-LINUX.json').write_text(json.dumps(dict(PASS=True,source='b98665626d623055f6bc60a170f32ae8206927b1',platform='linux',sequence=138,archive_sha256=v['archive_sha256'],components=dict(ui='2026.11.14.150',core='2026.11.13.18',gpu='2026.11.14.76',cpu='2026.11.6.389'),real_compiled_helper=True,actual_public_URLs=True,actual_authenticated_preparation=True,private_QA=str(root),wallet_unchanged=True,process_control=False,seconds=time.monotonic()-start,hosted_executor=True),indent=2)+'\n')
