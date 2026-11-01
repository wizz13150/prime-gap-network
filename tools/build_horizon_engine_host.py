"""Freeze Horizon's computation-only engine host; never packages Scout UI."""
from pathlib import Path
import argparse,subprocess,sys,ssl,os
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',required=True,type=Path);args=parser.parse_args();root=Path(__file__).resolve().parents[1];source=root/'src/engine/runtime_host';out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
command=[sys.executable,'-m','PyInstaller','--noconfirm','--onedir','--console','--name','horizon-engine-host','--distpath',str(out/'dist'),'--workpath',str(out/'work'),'--specpath',str(out/'spec'),'--paths',str(source),'--paths',str(root/'src/protocol'),'--hidden-import','sqlite3','--hidden-import','_sqlite3']
for p in source.glob('pgn_*.py'):command+=['--hidden-import',p.stem]
# Some managed Python distributions load an additional certificate policy from
# ssl.py itself. Preserve that runtime dependency instead of weakening TLS.
certificate_policy=Path(ssl.__file__).parent/'codex-rejected-cas.pem'
if certificate_policy.is_file():command+=['--add-data',str(certificate_policy)+os.pathsep+'.']
for name in('rich','numpy','matplotlib','scipy','pandas','torch','tensorflow','cupy','numba'):command+=['--exclude-module',name]
command.append(str(source/'horizon_engine_host.py'));subprocess.run(command,check=True)
