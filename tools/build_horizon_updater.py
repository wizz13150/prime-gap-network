"""Freeze the optional updater with explicit dependency/source provenance."""
from pathlib import Path
import argparse
import hashlib
import importlib.metadata
import json
import os
import ssl
import subprocess
import sys


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',required=True,type=Path)
    parser.add_argument('--dependencies',required=True,type=Path)
    args=parser.parse_args()
    root=Path(__file__).resolve().parents[1]
    source=root/'src/update';dependencies=args.dependencies.resolve(strict=True)
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    sources=list(source.glob('horizon_*.py'))+[root/'tools'/n for n in
        ('stage_component_bundle.py','build_component_bundle.py','check_public_release.py','build_horizon_updater.py')]
    sha=lambda f:hashlib.sha256(f.read_bytes()).hexdigest()
    inputs={p.relative_to(root).as_posix():sha(p)for p in sources}
    versions={d.metadata['Name']:d.version for d in importlib.metadata.distributions(path=[str(dependencies)])
              if d.metadata['Name'].lower()not in ('pip','setuptools','wheel')}
    for name,value in {'tuf':'7.0.1','securesystemslib':'1.6.0','cryptography':'50.0.2'}.items():
        if versions.get(name)!=value:raise RuntimeError('Pinned update dependency differs: '+name)
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--onedir','--console',
        '--name','horizon-updater','--distpath',str(out/'dist'),'--workpath',str(out/'work'),
        '--specpath',str(out/'spec'),'--paths',str(dependencies),'--paths',str(source),
        '--paths',str(root/'tools')]
    for name in versions:command+=['--copy-metadata',name]
    certificate_policy=Path(ssl.__file__).parent/'codex-rejected-cas.pem'
    if certificate_policy.is_file():command+=['--add-data',str(certificate_policy)+os.pathsep+'.']
    for name in ('numpy','matplotlib','scipy','pandas','torch','tensorflow','cupy','numba','setuptools','pkg_resources'):
        command+=['--exclude-module',name]
    command.append(str(source/'horizon_update_cli.py'))
    environment=dict(os.environ,PYTHONPATH=str(dependencies))
    subprocess.run(command,check=True,env=environment)
    omitted=[]
    for name in ('cryptography-50.0.2','securesystemslib-1.6.0','tuf-7.0.1'):
        marker=out/'dist/horizon-updater/_internal'/(name+'.dist-info')/'REQUESTED'
        if marker.exists():
            if marker.is_symlink()or marker.stat().st_size!=0:raise RuntimeError('Unexpected installer metadata marker')
            marker.unlink();omitted.append(marker.relative_to(out/'dist/horizon-updater').as_posix())
    if any(sha(root/name)!=expected for name,expected in inputs.items()):raise RuntimeError('Updater source changed while freezing')
    image=out/'dist/horizon-updater/horizon-updater.exe'
    result=dict(schema='PGN_UPDATER_SOURCE_V1',sources=inputs,dependencies=versions,
        executable_sha256=sha(image),python=sys.version,scope='Build identity only; network and installer tests are separate')
    result['omitted_optional_installer_metadata']=omitted
    (out/'UPDATER-SOURCE.json').write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps(dict(PASS=True,image=str(image),sha256=sha(image))),flush=True)
if __name__=='__main__':main()
