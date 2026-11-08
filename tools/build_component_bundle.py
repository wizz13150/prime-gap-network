"""Build a bounded update payload from the exact public component inventory.

Local publisher preparation only: no signing, upload, execution or installation.
Uses the same path/context/dependency validator as the receiving client.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import tempfile
import zipfile
import stage_component_bundle as stage
from check_public_release import inspect

PRIMARY = {'ui': 'PGN-Horizon.exe', 'core': 'Core/pgn-core.exe',
           'gpu': 'EngineRuntime/network/horizon-engine-host.exe',
           'cpu': 'EngineRuntime/cpu/horizon-cpu-controller.exe'}
COMPONENT_PREFIX = {'ui': '', 'core': 'Core/', 'gpu': 'EngineRuntime/',
                    'cpu': 'EngineRuntime/cpu/'}


def validate_update_deployment(root, declared_paths, context, abi):
    """The delivered UI must be able to invoke its own authenticated updater."""
    helper = 'UpdateRuntime/horizon-updater.exe'
    if helper not in declared_paths:
        return
    descriptor = 'UpdateRuntime/deployment.json'
    stage.require(descriptor in declared_paths,
                  'Update helper is declared without its deployment.json')
    value = stage._json(stage._read_plain(root / descriptor, 8192))
    stage.require(value.get('schema') == 'PGN_UPDATE_DEPLOYMENT_V1'
                  and value.get('network') == context['network']
                  and value.get('genesis_hash') == context['genesis_hash']
                  and value.get('compatibility_set') == context['compatibility_set']
                  and value.get('abi') == abi and value.get('helper') == helper
                  and value.get('owned_update_protocol') == 1,
                  'Update deployment does not match the delivered cohort')
    with (root / helper).open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    stage.require(value.get('sha256') == digest,
                  'Update deployment helper fingerprint differs')


def build(cohort, context_path, output, sequence, release_version, abi):
    root = stage._plain_chain(cohort)
    context, context_hash = stage.read_context(context_path)
    inventory = stage._json(stage._read_plain(root/'components.json', stage.MAX_MANIFEST))
    stage.require(inventory.get('schema') == 'PGN_COMPONENT_MANIFEST_V1'
                  and inventory.get('network') == context['network'], 'Explicit matching component inventory required')
    components, sources, final_paths, omitted = [], {}, set(), []
    declared = inventory.get('components', [])
    stage.require(type(declared) is list and 1 <= len(declared) <= 4, 'Bounded component inventory required')
    versions = {row['id']: row['version'] for row in declared}
    for row in declared:
        component = row['id']
        stage.require(component in PRIMARY, 'Unknown component')
        files = []
        for item in row['files']:
            relative = COMPONENT_PREFIX[component]+item['path']
            name = 'payload/'+component+'/'+relative
            stage._code_path(name, component)
            stage.require(relative.casefold() not in final_paths, 'Components overlap an installation path')
            final_paths.add(relative.casefold())
            path = stage._plain_chain(root/relative)
            stage.require(path.is_relative_to(root) and path.is_file(), 'Source is outside cohort')
            stage.require(path.stat().st_size == item['bytes'] <= stage.MAX_FILE, 'Source size differs')
            with path.open('rb') as stream:
                actual = hashlib.file_digest(stream, 'sha256').hexdigest()
            stage.require(actual == item['sha256'], 'Source fingerprint differs')
            # pip's empty REQUESTED flags describe how build dependencies were
            # installed. They are not runtime metadata. Do not weaken the
            # client's nonempty-artifact contract to ship these markers.
            markers=('cryptography-50.0.2','securesystemslib-1.6.0','tuf-7.0.1')
            if component=='ui' and item['bytes']==0 and item['path'] in {
                    'UpdateRuntime/_internal/'+name+'.dist-info/REQUESTED'for name in markers}:
                omitted.append(relative)
                continue
            sources[name] = path
            files.append(dict(path=name, bytes=item['bytes'], sha256=actual))
        dependencies = ([dict(id='core',version=versions['core'],abi=abi)]
                        if component in ('gpu','cpu') and 'core' in versions else [])
        if component=='ui' and (root/'assets/audit-animation.rcc').is_file():
            stage.require('core' in versions,'The global audit renderer requires its Core reader')
            dependencies.append(dict(id='core',version=versions['core'],abi=abi))
        if component=='gpu' and (root/'pgn-native-gap-evidence.exe').is_file():
            stage.require('ui'in versions,'The shared proof helper requires its UI component')
            dependencies.append(dict(id='ui',version=versions['ui'],abi=abi))
        components.append(dict(id=component,version=row['version'],abi=abi,
            primary='payload/'+component+'/'+PRIMARY[component],dependencies=dependencies,files=files))
    actual_files = {p.relative_to(root).as_posix() for p in root.rglob('*') if p.is_file()}
    stage.require(actual_files == {str(p.relative_to(root)).replace('\\','/') for p in sources.values()} | set(omitted) | {'components.json'},
                  'Cohort contains undeclared files')
    validate_update_deployment(root, actual_files, context, abi)
    manifest = dict(schema='PGN_COMPONENT_STAGE_BUNDLE_V1',bundle_id='pgn.'+str(sequence),
        sequence=sequence,release_version=release_version,network=context['network'],
        genesis_hash=context['genesis_hash'],platform=context['platform'],
        compatibility_set=context['compatibility_set'],components=components)
    encoded = json.dumps(manifest,separators=(',',':')).encode()
    stage._manifest(encoded,context,list(versions))
    destination = stage._plain_chain(output)
    stage.require(not destination.exists() and destination.parent.is_dir(), 'New output in an existing folder required')
    handle, temporary = tempfile.mkstemp(prefix='.pgn-bundle-',suffix='.zip',dir=destination.parent)
    os.close(handle)
    try:
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
            archive.writestr('component-bundle.json',encoded)
            for name,path in sorted(sources.items()):
                archive.write(path,name)
        stage.require(Path(temporary).stat().st_size <= stage.MAX_ARCHIVE,'Compressed update archive exceeds bound')
        privacy = inspect(temporary)
        stage.require(privacy['PASS'],'Public configuration check failed: '+str(privacy['rejected']))
        # Re-read the real archive through the exact client preflight, including
        # bounded entries/ratios and source hashes; no payload is executed.
        with open(temporary,'rb') as stream:
            stage._zip_preflight(stream)
        os.link(temporary,destination)
    finally:
        Path(temporary).unlink(missing_ok=True)
    return dict(schema='PGN_COMPONENT_BUILD_RECEIPT_V1',archive_sha256=privacy['archive_sha256'],
        bytes=destination.stat().st_size,expanded_bytes=privacy['expanded_bytes'],files=len(sources),
        manifest=manifest,context_sha256=context_hash,privacy=privacy,
        omitted_optional_installer_metadata=omitted,installation_performed=False)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cohort',type=Path,required=True);parser.add_argument('--context',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True);parser.add_argument('--sequence',type=int,required=True)
    parser.add_argument('--release-version',required=True);parser.add_argument('--abi',required=True)
    args=parser.parse_args()
    result=build(args.cohort,args.context,args.output,args.sequence,args.release_version,args.abi)
    print(json.dumps(result,separators=(',',':')))
if __name__=='__main__':main()
