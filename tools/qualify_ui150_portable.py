from pathlib import Path
import hashlib,json,os,subprocess,tarfile,zipfile
P=Path.cwd();bundle=P/'portable.tar.gz';assert hashlib.sha256(bundle.read_bytes()).hexdigest()=='7943b348c6db577c780e0026f2291b3cd90e48d37d4e64d7430fd144c2cb40ee'
archive=P/'ui.zip';assert hashlib.sha256(archive.read_bytes()).hexdigest()=='2cbff1092bbf2a92eae5ad65a475fc684c26d36d024af4e86bf3f76389d8157b'
with tarfile.open(bundle)as z:z.extractall(P/'isolated',filter='data')
candidates=[]
for f in(P/'isolated').rglob('components.json'):
 v=json.loads(f.read_bytes())
 if any(x['id']=='ui'and x['version']=='2026.11.14.149'for x in v.get('components',[])):candidates.append(f.parent)
assert len(candidates)==1,candidates
root=candidates[0];exe=root/'PGN-Horizon';assert exe.is_file()
with zipfile.ZipFile(archive)as z:
 assert z.namelist()==['PGN-Horizon'];raw=z.read('PGN-Horizon');assert hashlib.sha256(raw).hexdigest()=='5c8315fc7c5f2ad45860035d49db443ffca920a594f2446b2b6f1851821b51ed'
exe.write_bytes(raw);exe.chmod(0o700)
env=dict(os.environ,QT_QPA_PLATFORM='offscreen',XDG_CONFIG_HOME=str(P/'config'),XDG_DATA_HOME=str(P/'data'))
x=subprocess.run(['ldd',str(exe)],env=env,capture_output=True,timeout=15)
(P/'loader-dependencies.txt').write_bytes(x.stdout+x.stderr)
assert x.returncode==0 and b'not found'not in x.stdout,(x.returncode,x.stdout.decode()[-4000:],x.stderr.decode()[-1000:])
(P/'loader-dependencies.txt').write_bytes(x.stdout)
x=subprocess.run([str(exe),'--show-owned','invalid','--inspect-owned'],env=env,cwd=root,capture_output=True,timeout=15);assert x.returncode==2,(x.returncode,x.stderr[-2000:])
(P/'PORTABLE-LOADER.json').write_text(json.dumps(dict(PASS=True,source='b98665626d623055f6bc60a170f32ae8206927b1',image_sha256=hashlib.sha256(raw).hexdigest(),public136_dependency_closure_unchanged=True,existing_portable_sha256='7943b348c6db577c780e0026f2291b3cd90e48d37d4e64d7430fd144c2cb40ee',process_exit=x.returncode,live_network_or_wallet_started=False),indent=2)+'\n')
