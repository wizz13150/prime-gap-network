"""Build one coherent Linux testnet runtime from this source checkout.

Qt UI, Core, host, updater and CPU controller are rebuilt. Qualified native
GPU/CPU images and their licences are recovered from one SHA256-pinned public
input archive; its old Core, UI, profiles and network configuration are never
used. No wallet, chain data or running application is inspected or changed.
"""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,os,platform,shutil,subprocess,sys,sysconfig,tarfile,tempfile,urllib.request

ROOT=Path(__file__).resolve().parents[1]
GENESIS='0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d'
ABI='pgn.epoch2.2026.11'
INPUT_URL='https://github.com/wizz13150/prime-gap-network/releases/download/v0.1.0-alpha.1/PGN-Horizon-testnet-preview-Ubuntu24.04-x86_64.tar.gz'
INPUT_SHA='0bc3768ecb953b789b5ea79e1d506ba71333451b260b24f8ce4a8c719a0d6eee'
INPUT_BYTES=107844589
VERSIONS=dict(ui='2026.11.9.105',core='2026.11.9.8',gpu='2026.11.9.41',cpu='2026.11.6.389')


def sha(path):
    with Path(path).open('rb')as stream:return hashlib.file_digest(stream,'sha256').hexdigest()


def run(*args):subprocess.run(list(map(str,args)),check=True,cwd=ROOT)


def native_inputs(work):
    archive=work/'qualified-native-inputs.tar.gz'
    if not archive.exists():
        pending=archive.with_suffix('.pending')
        if pending.exists():raise ValueError('Previous incomplete input download retained: '+str(pending))
        request=urllib.request.Request(INPUT_URL,headers={'User-Agent':'PGN-Horizon-build'})
        try:
            with urllib.request.urlopen(request,timeout=45)as source,pending.open('xb')as destination:
                total=0
                while data:=source.read(1024*1024):
                    total+=len(data)
                    if total>INPUT_BYTES:raise ValueError('Native input length exceeds the pinned archive')
                    destination.write(data)
            if pending.stat().st_size!=INPUT_BYTES or sha(pending)!=INPUT_SHA:raise ValueError('Native input archive identity differs')
            pending.replace(archive)
        finally:pending.unlink(missing_ok=True)
    if archive.stat().st_size!=INPUT_BYTES or sha(archive)!=INPUT_SHA:raise ValueError('Cached native input archive identity differs')
    destination=work/'native-inputs';destination.mkdir(exist_ok=True)
    with tarfile.open(archive)as source:
        for item in source:
            parts=PurePosixPath(item.name).parts
            if not parts or parts[0]!='PGN-Horizon' or '..'in parts:raise ValueError('Invalid pinned input path')
            relative='/'.join(parts[1:])
            chosen=(relative.startswith(('EngineRuntime/bin/prime-gap-worker-',
                'EngineRuntime/lib/','EngineRuntime/THIRD-PARTY-LICENSES/'))
                or relative=='EngineRuntime/THIRD-PARTY-NOTICES.txt'
                or relative in ('EngineRuntime/cpu/_internal/native/horizon-cpu-inventory',
                    'EngineRuntime/cpu/_internal/native/horizon-cpu-shared68-worker'))
            if not chosen or item.isdir():continue
            if item.issym()or not(item.isfile()or item.islnk()):raise ValueError('Unsupported native input entry')
            stream=source.extractfile(item)
            if stream is None:raise ValueError('Native input bytes are unavailable')
            target=destination.joinpath(*parts[1:]);target.parent.mkdir(parents=True,exist_ok=True)
            with stream,target.open('wb')as out:shutil.copyfileobj(stream,out)
            with target.open('rb')as image:mode=0o755 if image.read(4)==b'\x7fELF' else 0o644
            target.chmod(mode)
    sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/engine/cpu_auditor/shared_service')]
    from horizon_engine_capabilities import qualified_workers
    from native_transport import verify,HASHES
    for architecture in (86,120):qualified_workers(destination/'EngineRuntime',architecture)
    native=destination/'EngineRuntime/cpu/_internal/native'
    verify(native/'horizon-cpu-inventory',HASHES['posix'][1])
    return destination


def cpu_freeze_source(work,app,inputs):
    """Bind the tested native build into the frozen controller, never at runtime."""
    run('cmake','--build',app,'--target','pgn-cpu-composite-worker-test-helper',
        'pgn-cpu-composite-overlay-test-helper','--parallel',2)
    worker=app/'pgn-cpu-composite-worker-test-helper'
    run(sys.executable,'-I',ROOT/'tests/test_cpu_composite_overlay.py',worker,
        app/'pgn-cpu-composite-overlay-test-helper')
    source=work/'cpu-freeze-source'
    source.mkdir(exist_ok=True)
    for path in (ROOT/'src/engine/cpu_auditor/shared_service').glob('*.py'):
        shutil.copyfile(path,source/path.name)
    inventory=inputs/'EngineRuntime/cpu/_internal/native/horizon-cpu-inventory'
    identity=dict(posix=(sha(worker),sha(inventory)))
    (source/'native_image_identity.py').write_text(
        '"""Generated from the tested native Linux build; frozen into the controller."""\nHASHES='+repr(identity)+'\n',encoding='utf-8')
    native=work/'cpu-native';native.mkdir(exist_ok=True)
    shutil.copy2(worker,native/'horizon-cpu-shared68-worker')
    shutil.copy2(inventory,native/'horizon-cpu-inventory')
    return source,native


def build(work,jobs,inputs):
    app=work/'app';core=work/'core';host=work/'host';updater=work/'updater';cpu=work/'cpu'
    run('cmake','-S',ROOT,'-B',app,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release')
    run('cmake','--build',app,'--target','PGN-Horizon','pgn-gap-evidence','--parallel',jobs)
    run('cmake','-S',ROOT/'wallet/riecoin-core-upstream-v2606','-B',core,'-G','Ninja',
        '-DCMAKE_BUILD_TYPE=Release','-DBUILD_GUI=OFF','-DBUILD_TESTS=OFF','-DBUILD_BENCH=OFF',
        '-DBUILD_DAEMON=ON','-DBUILD_CLI=ON','-DENABLE_WALLET=ON','-DWITH_ZMQ=OFF','-DWITH_USDT=OFF')
    run('cmake','--build',core,'--target','riecoind','riecoin-cli','--parallel',jobs)
    run(sys.executable,ROOT/'tools/build_horizon_engine_host.py','--output',host)
    run(sys.executable,ROOT/'tools/build_horizon_updater.py','--output',updater,
        '--dependencies',sysconfig.get_path('purelib'))
    service,native=cpu_freeze_source(work,app,inputs)
    command=[sys.executable,'-m','PyInstaller','--noconfirm','--onedir','--console',
        '--name','horizon-cpu-controller','--distpath',str(cpu/'dist'),'--workpath',str(cpu/'work'),
        '--specpath',str(cpu/'spec'),'--paths',str(service)]
    for name in ('horizon-cpu-inventory','horizon-cpu-shared68-worker'):
        command+=['--add-binary',str(native/name)+':native']
    for name in ('numpy','torch','tensorflow','matplotlib','scipy','pandas'):
        command+=['--exclude-module',name]
    run(*command,service/'service.py')
    return app,core,host,updater,cpu


def copy_tree(source,target):
    # Freeze outputs may contain library aliases. The final code inventory is
    # a plain-file tree; copies dereference those build-owned aliases.
    shutil.copytree(source,target,symlinks=False,dirs_exist_ok=True)


def assemble(output,inputs,app,core,host,updater,cpu):
    if output.exists():raise ValueError('Use a new output directory; existing runtime is retained')
    output.mkdir(parents=True)
    shutil.copy2(app/'PGN-Horizon',output/'PGN-Horizon')
    shutil.copy2(app/'pgn-gap-evidence',output/'pgn-native-gap-evidence')
    assets=app/'assets/audit-animation.rcc'
    if assets.exists():
        (output/'assets').mkdir();shutil.copy2(assets,output/'assets/audit-animation.rcc')
    core_input=output.parent/(output.name+'-core-input');core_input.mkdir(exist_ok=False)
    shutil.copy2(core/'bin/riecoind',core_input/'pgn-core')
    from package_core_bundle import assemble as assemble_core
    assemble_core(core_input/'pgn-core',[],output/'Core',chain='test',genesis=GENESIS,
                  rpc_port=48982,peer_port=48983,peers=[])
    copy_tree(inputs/'EngineRuntime/bin',output/'EngineRuntime/bin')
    copy_tree(inputs/'EngineRuntime/lib',output/'EngineRuntime/lib')
    copy_tree(inputs/'EngineRuntime/THIRD-PARTY-LICENSES',output/'EngineRuntime/THIRD-PARTY-LICENSES')
    notices=inputs/'EngineRuntime/THIRD-PARTY-NOTICES.txt'
    if notices.exists():shutil.copy2(notices,output/'EngineRuntime'/notices.name)
    copy_tree(host/'dist/horizon-engine-host',output/'EngineRuntime/network')
    copy_tree(cpu/'dist/horizon-cpu-controller',output/'EngineRuntime/cpu')
    copy_tree(updater/'dist/horizon-updater',output/'UpdateRuntime')
    bootstrap=dict(schema='PGN_TESTNET_BOOTSTRAP_V1',genesis=GENESIS,
        tls=dict(host='wizz.tail6acec6.ts.net',port=8443),
        checkpoint=dict(height=13,hash='71418e43bc8576355e27166b040f8dd26dea7ed86fd735da485bf2671db15037'))
    write=lambda p,v:p.write_text(json.dumps(v,sort_keys=True,separators=(',',':'))+'\n',encoding='utf-8')
    write(output/'EngineRuntime/network/bootstrap.json',bootstrap)
    helper=output/'UpdateRuntime/horizon-updater'
    write(output/'UpdateRuntime/deployment.json',dict(schema='PGN_UPDATE_DEPLOYMENT_V1',network='testnet',
        genesis_hash=GENESIS,compatibility_set=ABI,abi=ABI,helper='UpdateRuntime/horizon-updater',
        sha256=sha(helper),owned_update_protocol=1))
    launcher=output/'launch-horizon.sh'
    launcher.write_text(r'''#!/bin/sh
set -eu
for key in $(env | sed -n 's/^\(SNAP[A-Z_]*\)=.*/\1/p'); do unset "$key"; done
case "${LD_LIBRARY_PATH-}" in */snap/*|*/var/lib/snapd/*) unset LD_LIBRARY_PATH;; esac
case "${QT_PLUGIN_PATH-}" in */snap/*|*/var/lib/snapd/*) unset QT_PLUGIN_PATH;; esac
case "${QT_QPA_PLATFORM_PLUGIN_PATH-}" in */snap/*|*/var/lib/snapd/*) unset QT_QPA_PLATFORM_PLUGIN_PATH;; esac
exec "$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)/PGN-Horizon" "$@"
''',encoding='utf-8');launcher.chmod(0o755)
    (output/'README.txt').write_text('PGN Horizon testnet. Run ./launch-horizon.sh. Qt 6 runtime packages must be installed.\nThe Wallet management page opens when no wallet is configured.\nMining never requires diagnostic recording or manual admission.\n',encoding='utf-8')
    from build_component_bundle import COMPONENT_PREFIX,validate_update_deployment,validate_network_bootstrap
    rows={name:[]for name in VERSIONS}
    for path in sorted(output.rglob('*')):
        if not path.is_file():continue
        relative=path.relative_to(output).as_posix()
        identity='cpu'if relative.startswith('EngineRuntime/cpu/')else 'gpu'if relative.startswith('EngineRuntime/')else 'core'if relative.startswith('Core/')else 'ui'
        rows[identity].append(dict(path=relative[len(COMPONENT_PREFIX[identity]):],bytes=path.stat().st_size,sha256=sha(path)))
    manifest=dict(schema='PGN_COMPONENT_MANIFEST_V1',network='testnet',platform='linux-x86_64',
        release_version=VERSIONS['ui'],published=True,components=[dict(id=k,version=v,files=rows[k])for k,v in VERSIONS.items()])
    write(output/'components.json',manifest)
    context=dict(network='testnet',genesis_hash=GENESIS,platform='linux-x86_64',compatibility_set=ABI)
    declared={p.relative_to(output).as_posix()for p in output.rglob('*')if p.is_file()}
    validate_update_deployment(output,declared,context,ABI);validate_network_bootstrap(output,declared,context)
    from horizon_component_install import inventory
    return inventory(output)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--work-dir',type=Path,default=ROOT/'build/linux-runtime-work')
    parser.add_argument('--jobs',type=int,default=2);parser.add_argument('--assemble-only',action='store_true')
    for name in ('app','core','host','updater','cpu'):parser.add_argument('--'+name+'-build',type=Path)
    args=parser.parse_args()
    if sys.platform!='linux'or platform.machine()!='x86_64':raise ValueError('Native Linux x86_64 build required')
    if not 1<=args.jobs<=8:raise ValueError('Choose one to eight build jobs')
    sys.path[:0]=[str(ROOT/'tools'),str(ROOT/'src/update')]
    work=args.work_dir.resolve();work.mkdir(parents=True,exist_ok=True)
    if shutil.disk_usage(work).free<3*1024**3:raise ValueError('Free at least 3 GiB before building')
    inputs=native_inputs(work)
    if args.assemble_only:
        paths=tuple(getattr(args,name+'_build')for name in ('app','core','host','updater','cpu'))
        if any(p is None for p in paths):raise ValueError('Assemble-only requires all five exact build folders')
    else:paths=build(work,args.jobs,inputs)
    output=args.output.resolve()
    if output.exists():raise ValueError('Use a new output directory; existing runtime is retained')
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.horizon-linux-build-',dir=output.parent)as folder:
        staged=Path(folder)/'runtime'
        value,files,digest,total=assemble(staged,inputs,*paths)
        if output.exists():raise ValueError('Output appeared while building; existing runtime is retained')
        staged.rename(output)
    print(json.dumps(dict(schema='PGN_LINUX_RUNTIME_BUILD_V1',PASS=True,inventory_sha256=digest,files=len(files),bytes=total,
        qualified_native_input_sha256=INPUT_SHA,versions=VERSIONS,network_or_mining_started=False)))


if __name__=='__main__':main()
