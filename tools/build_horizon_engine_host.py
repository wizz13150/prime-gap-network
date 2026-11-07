"""Freeze Horizon's computation-only engine host; never packages Scout UI."""
from pathlib import Path
import argparse,subprocess,sys,ssl,os,hashlib,json
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path);args=parser.parse_args();root=Path(__file__).resolve().parents[1];source=root/'src/engine/runtime_host';out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
inputs={p.relative_to(root).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for folder in (source,root/'src/protocol',root/'src/engine/shared_composite')for p in sorted(folder.glob('*.py'))}
inputs[Path(__file__).resolve().relative_to(root).as_posix()]=hashlib.sha256(Path(__file__).read_bytes()).hexdigest()
for name in ('export_gap_observations.py','export_scientific_results.py'):
 p=root/'tools'/name;inputs[p.relative_to(root).as_posix()]=hashlib.sha256(p.read_bytes()).hexdigest()
command=[sys.executable,'-m','PyInstaller','--noconfirm','--onedir','--console','--name','horizon-engine-host','--distpath',str(out/'dist'),'--workpath',str(out/'work'),'--specpath',str(out/'spec'),'--paths',str(source),'--paths',str(root/'src/protocol'),'--paths',str(root/'src/engine/shared_composite'),'--hidden-import','sqlite3','--hidden-import','_sqlite3']
for p in source.glob('pgn_*.py'):command+=['--hidden-import',p.stem]
# Some managed Python distributions load an additional certificate policy from
# ssl.py itself. Preserve that runtime dependency instead of weakening TLS.
certificate_policy=Path(ssl.__file__).parent/'codex-rejected-cas.pem'
if certificate_policy.is_file():command+=['--add-data',str(certificate_policy)+os.pathsep+'.']
for name in('rich','numpy','matplotlib','scipy','pandas','torch','tensorflow','cupy','numba'):command+=['--exclude-module',name]
command += ['--paths',str(root/'tools')]
command.append(str(source/'horizon_engine_host.py'));subprocess.run(command,check=True)
if any(hashlib.sha256((root/name).read_bytes()).hexdigest()!=expected for name,expected in inputs.items()):
    raise RuntimeError('Engine-host source changed while freezing; output is not qualified')
executable=out/'dist/horizon-engine-host/horizon-engine-host.exe'
manifest=dict(schema='PGN_HORIZON_ENGINE_HOST_SOURCE_V1',sources=inputs,
              executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
              python_version=sys.version,scope='Build provenance only; no runtime, network or scientific acceptance')
raw=(json.dumps(manifest,sort_keys=True,separators=(',',':'))+'\n').encode('utf-8')
temporary=out/'ENGINE-HOST-SOURCE.json.tmp';temporary.write_bytes(raw);temporary.replace(out/'ENGINE-HOST-SOURCE.json')
(out/'ENGINE-HOST-SOURCE.sha256').write_bytes((hashlib.sha256(raw).hexdigest()+'\n').encode('ascii'))
