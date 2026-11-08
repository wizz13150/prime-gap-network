"""Authenticate component discovery/downloads with python-tuf.

This module never launches a payload, changes installed code, controls a miner,
or accesses wallet files. Activation remains a separate lifecycle transaction.
The caller supplies the build-pinned bootstrap root, not a downloaded trust key.
"""
import hashlib
import json
import os
import uuid
from pathlib import Path
import re
from urllib.parse import urlsplit

from tuf.ngclient import Updater
from tuf.ngclient.config import UpdaterConfig
from tuf.api.exceptions import LengthOrHashMismatchError
import stage_component_bundle as stage
import horizon_update_progress as progress
from tuf.ngclient.urllib3_fetcher import Urllib3Fetcher

MAX_CATALOG = 128 * 1024
MAX_CACHE = 1024 * 1024 * 1024
MAX_TARGET = 512 * 1024 * 1024
MARKER = b'PGN_AUTHENTICATED_UPDATE_CACHE_V1\n'
NAME = re.compile(r'[A-Za-z0-9][A-Za-z0-9._-]{0,180}\Z')


def digest(data):
    return hashlib.sha256(data).hexdigest()


# Shared OS lock also protects staging/installation writers across crashes.
locked_cache = stage.owned_lock

class AuthenticatedUpdates:
    def __init__(self, cache, bootstrap, bootstrap_sha256, metadata_url,
                 target_url, context, context_sha256, *, fetcher=None):
        stage.require(type(bootstrap) is bytes and len(bootstrap) <= 262144
                      and stage.DIGEST.fullmatch(bootstrap_sha256)
                      and digest(bootstrap) == bootstrap_sha256,
                      'Build-pinned bootstrap root differs')
        for url in (metadata_url, target_url):
            parsed = urlsplit(url)
            stage.require(parsed.scheme == 'https' and parsed.hostname
                          and not parsed.username and not parsed.password
                          and not parsed.query and not parsed.fragment
                          and url.endswith('/'), 'Explicit HTTPS repository required')
        self.context, self.context_sha256 = context, context_sha256
        self.cache_repairs = []
        self.cache = stage._cache_path(cache, context)
        if self.cache.exists():
            stage.require(stage._read_plain(self.cache / '.owner', 128) == MARKER,
                          'Unowned update cache refused')
        else:
            self.cache.mkdir(parents=True, mode=0o700)
            (self.cache / '.owner').write_bytes(MARKER)
        self.bootstrap, self.bootstrap_sha256 = bootstrap, bootstrap_sha256
        self.metadata_url, self.target_url = metadata_url, target_url
        self.fetcher=progress.ProgressFetcher(fetcher or Urllib3Fetcher(app_user_agent='PGN-Horizon-Updater'))
        namespace=self.cache/bootstrap_sha256[:20]
        stage._plain_chain(namespace);namespace.mkdir(parents=True,exist_ok=True)
        marker=namespace/'.bootstrap-sha256';expected=(bootstrap_sha256+'\n').encode()
        if not marker.exists():
            temporary=namespace/('.bootstrap-'+uuid.uuid4().hex+'.pending')
            try:
                with temporary.open('xb')as stream:stream.write(expected);stream.flush();os.fsync(stream.fileno())
                try:os.link(temporary,marker)
                except FileExistsError:pass
            finally:temporary.unlink(missing_ok=True)
        stage.require(stage._read_plain(marker,65)==expected,'Update cache namespace belongs to another full bootstrap root')
        self.metadata = namespace / 'metadata'
        self.targets = namespace / 'targets'
        stage._plain_chain(self.metadata)
        stage._plain_chain(self.targets)
        self.metadata.mkdir(parents=True, exist_ok=True)
        self.targets.mkdir(parents=True, exist_ok=True)

    def _quota(self, additional=0):
        total = 0
        for base, dirs, files in os.walk(self.cache, followlinks=False):
            for name in dirs + files:
                file = Path(base) / name
                if stage._linked(file):
                    # python-tuf7 intentionally maintains this ONE relative
                    # alias. Windows without symlink permission uses a hardlink.
                    # The target still undergoes TUF root-chain verification.
                    stage.require(file == self.metadata / 'root.json' and file.is_symlink(),
                                  'Linked update-cache entry')
                    link = os.readlink(file).replace('\\', '/')
                    stage.require(re.fullmatch(r'root_history/[1-9][0-9]{0,18}\.root\.json', link),
                                  'Unexpected TUF root alias')
                    target = stage._plain_chain(self.metadata / link)
                    stage.require(target.is_file() and target.stat().st_size <= 262144,
                                  'TUF root alias target is unavailable or outside bounds')
            for name in files:
                total += (Path(base) / name).stat().st_size
                stage.require(total + additional <= MAX_CACHE, 'Update cache quota exceeded')

    def _updater(self):
        progress.report('metadata',force=True)
        return Updater(str(self.metadata), self.metadata_url, str(self.targets),
                       self.target_url, fetcher=self.fetcher, bootstrap=self.bootstrap,
                       config=UpdaterConfig(max_root_rotations=32, max_delegations=1,
                           root_max_length=262144, timestamp_max_length=16384,
                           snapshot_max_length=262144, targets_max_length=1048576,
                           app_user_agent='PGN-Horizon-Updater'))

    def _download(self, updater, name, maximum):
        stage.require(type(name) is str and NAME.fullmatch(name), 'Flat bounded target name required')
        target = updater.get_targetinfo(name)
        stage.require(target is not None and type(target.length) is int
                      and 0 < target.length <= maximum
                      and stage.DIGEST.fullmatch(target.hashes.get('sha256', '')),
                      'Signed target length/SHA256 unavailable or outside bounds')
        destination = self.targets / (target.hashes['sha256'] + '-' + name)
        stage._plain_chain(destination)
        self._quota(0 if destination.exists() else target.length)
        if destination.exists():
            try:
                with destination.open('rb') as stream:
                    target.verify_length_and_hashes(stream)
            except LengthOrHashMismatchError:
                # Interrupted downloads are disposable derived code. Remove
                # only this exact plain cached target, then authenticate its
                # replacement normally; never relax the expected digest.
                stage._plain_chain(destination)
                destination.unlink()
                self.cache_repairs.append(name)
        if not destination.exists():
            import shutil,tempfile
            for folder in (self.targets,Path(tempfile.gettempdir())):
                stage.require(shutil.disk_usage(folder).free>=target.length+16*1024*1024,'Insufficient download disk space; free space and retry')
            if name.endswith('.zip'):progress.report('download',completed=0,total=target.length,force=True)
            self.fetcher.target_length=target.length if name.endswith('.zip')else 0
            try:updater.download_target(target, filepath=str(destination))
            finally:self.fetcher.target_length=0
        stage._plain_chain(destination)
        with destination.open('rb') as stream:
            target.verify_length_and_hashes(stream)
        if name.endswith('.zip'):progress.report('download',completed=target.length,total=target.length,force=True)
        return destination, target

    def _catalog(self, updater, selected):
        updater.refresh()
        file, target = self._download(updater, 'components.json', MAX_CATALOG)
        catalog = stage._json(stage._read_plain(file, MAX_CATALOG))
        stage._keys(catalog, 'schema bundle_target manifest')
        stage.require(catalog['schema'] == 'PGN_AUTHENTICATED_UPDATE_CATALOG_V1'
                      and type(catalog['bundle_target']) is str
                      and NAME.fullmatch(catalog['bundle_target'])
                      and catalog['bundle_target'].endswith('.zip'), 'Invalid authenticated catalogue')
        raw = json.dumps(catalog['manifest'], separators=(',', ':')).encode()
        manifest, components, _, chosen = stage._manifest(raw, self.context, selected)
        return catalog, manifest, components, chosen, target

    def refresh(self, selected):
        with locked_cache(self.cache):
            self._quota()
            catalog, manifest, components, chosen, target = self._catalog(self._updater(), selected)
            return dict(schema='PGN_AUTHENTICATED_COMPONENT_VIEW_V1',
                        publisher_authenticated=True, bootstrap_sha256=self.bootstrap_sha256,
                        catalog_sha256=target.hashes['sha256'],
                        network=manifest['network'], genesis=manifest['genesis_hash'],
                        sequence=manifest['sequence'], release_version=manifest['release_version'],
                        components=[{key: components[c][key] for key in
                                    ('id', 'version', 'abi', 'dependencies')} for c in chosen],
                        bundle_target=catalog['bundle_target'], installation_performed=False)

    def prepare(self, selected):
        with locked_cache(self.cache):
            self._quota()
            updater = self._updater()
            catalog, manifest, _, _, target = self._catalog(updater, selected)
            archive, artifact = self._download(updater, catalog['bundle_target'], MAX_TARGET)
            selection = sorted(set(selected))
            key = digest(json.dumps([self.context_sha256, artifact.hashes['sha256'], selection]).encode())
            stage_cache = self.cache / ('s-'+key[:20])
            receipt_file = self.cache / 'receipts' / (key + '.json')
            stage._plain_chain(receipt_file)
            if receipt_file.exists():
                old = stage._json(stage._read_plain(receipt_file, MAX_CATALOG))
                folder = stage._plain_chain(old['stage'])
                stage.require(folder.parent == stage_cache, 'Retained stage escaped its namespace')
                plan = stage.verify_stage(folder, old['manifest_sha256'], self.context, self.context_sha256)
                stage.require(plan['archive_sha256'] == artifact.hashes['sha256']
                              and plan['requested'] == selection, 'Retained stage binding changed')
            else:
                _, components, files, chosen = stage._manifest(
                    json.dumps(manifest, separators=(',', ':')).encode(), self.context, selection)
                names = {f['path'] for c in chosen for f in components[c]['files']}
                self._quota(sum(files[n]['bytes'] for n in names) + 2 * MAX_CATALOG)
                if not stage_cache.exists():
                    stage_cache.parent.mkdir(parents=True, exist_ok=True)
                    stage.init_cache(stage_cache, self.context)
                folder, plan = stage.stage_bundle(archive, artifact.hashes['sha256'],
                                                 self.context, self.context_sha256,
                                                 stage_cache, selection,progress=progress.report)
            raw = stage._read_plain(folder / 'component-bundle.json', MAX_CATALOG)
            stage.require(stage._json(raw) == manifest,
                          'Signed catalogue and downloaded bundle describe different components')
            receipt = dict(schema='PGN_AUTHENTICATED_STAGE_RECEIPT_V1',
                        publisher_authenticated=True, bootstrap_sha256=self.bootstrap_sha256,
                        catalog_sha256=target.hashes['sha256'],
                        archive_sha256=artifact.hashes['sha256'],
                        manifest_sha256=plan['manifest_sha256'], context_sha256=self.context_sha256,
                        stage=str(folder), selection=plan['selected_with_dependencies'],
                        installation_performed=False, process_control_performed=False)
            receipt_file.parent.mkdir(parents=True, exist_ok=True)
            temporary = receipt_file.with_suffix('.pending')
            with temporary.open('wb') as stream:
                stream.write((json.dumps(receipt, sort_keys=True) + '\n').encode())
                stream.flush()
                os.fsync(stream.fileno())
            os.replace(temporary, receipt_file)
            self._quota()
            return receipt
