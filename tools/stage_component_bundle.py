"""Prepare local code bundles only. No network, installation, execution or trust grant.

CoreBundle invariants retained: explicit network/genesis/primary, bounded exact
file hashes, no data/wallet configuration. Qt IFW-style explicit components and
dependencies are resolved before writing. This is not a TUF implementation.
"""
from __future__ import annotations

import argparse
from contextlib import contextmanager
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import stat
import struct
import sys
import time
import uuid
import zipfile

# The complete four-component Windows payload is already ~248 MiB before the
# vanity/update helpers. Keep a bounded 512 MiB expanded staging transaction.
MAX_CACHE = 512 * 1024 * 1024
MAX_ARCHIVE = 256 * 1024 * 1024
MAX_FILE = 128 * 1024 * 1024
MAX_MANIFEST = 128 * 1024
MAX_ENTRIES = 512
MAX_RATIO = 200
CHUNK = 1024 * 1024
MARKER = b'PGN_COMPONENT_STAGE_CACHE_V1\n'
IDS = {'ui', 'core', 'cpu', 'gpu'}
DIGEST = re.compile(r'[0-9a-f]{64}\Z')
TOKEN = re.compile(r'[A-Za-z0-9_][A-Za-z0-9.+_-]{0,79}\Z')
VERSION = re.compile(r'(?:0|[1-9][0-9]{0,8})(?:[.-](?:0|[1-9][0-9]{0,8})){0,7}\Z')
RESERVED = {'con', 'prn', 'aux', 'nul', 'clock$', 'conin$', 'conout$'} | {
    f'{p}{i}' for p in ('com', 'lpt') for i in range(1, 10)}
DATA_PARTS = {'data', 'wallet', 'wallets', 'chainstate', 'blocks', 'indexes',
              'backups', 'logs', 'secrets', 'keys', 'credentials'}
CODE_SUFFIXES = {'.exe', '.dll', '.pyd', '.so', '.dylib', '.py', '.pyz', '.qml',
                 '.rcc', '.qm', '.svg', '.png', '.ico', '.ttf', '.otf'}
IMMUTABLE_RESOURCES = {
    'ui': {'data/reference/prime-gap-records.json', 'LIMITS.txt', 'README.txt', 'TESTING.txt',
           'UpdateRuntime/deployment.json', 'UpdateRuntime/_internal/codex-rejected-cas.pem'},
    'core': {'Core/core-template.json'},
    'gpu': {'EngineRuntime/bin/canonical-orders.bin',
            'EngineRuntime/network/bootstrap.json',
            'EngineRuntime/network/scientific-source.json',
            'EngineRuntime/network/_internal/codex-rejected-cas.pem',
            'EngineRuntime/lib/proof/windows/manifest.json',
            'EngineRuntime/vanity/runtime.json',
            'EngineRuntime/THIRD-PARTY-NOTICES.txt',
            'EngineRuntime/THIRD-PARTY-LICENSES/MANIFEST.json'},
    'cpu': set(),
}


class StageError(ValueError):
    pass


class Cancelled(StageError):
    pass


def require(condition, message):
    if not condition:
        raise StageError(message)


def sha(data):
    return hashlib.sha256(data).hexdigest()


def _pairs(pairs):
    result = {}
    for key, value in pairs:
        require(key not in result, 'Duplicate JSON key')
        result[key] = value
    return result


def _json(raw):
    require(len(raw) <= MAX_MANIFEST, 'Metadata exceeds bound')
    try:
        return json.loads(raw, object_pairs_hook=_pairs)
    except (UnicodeError, json.JSONDecodeError, RecursionError) as exc:
        raise StageError('Invalid bounded JSON metadata') from exc


def _keys(obj, expected):
    require(type(obj) is dict and set(obj) == set(expected.split()), 'Unexpected metadata fields')


def _integer(value, low, high):
    return type(value) is int and low <= value <= high


def _version(value):
    require(type(value) is str and VERSION.fullmatch(value), 'Explicit numeric component version required')
    return tuple(int(x) for x in re.split(r'[.-]', value))


def _older(left, right):
    a, b = _version(left), _version(right)
    n = max(len(a), len(b))
    return a + (0,) * (n - len(a)) < b + (0,) * (n - len(b))


def _linked(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) & 0x400)


def _plain_chain(path):
    path = Path(os.path.abspath(path))
    for part in (path, *path.parents):
        if os.path.lexists(part):
            require(not _linked(part), 'Symlink/reparse path is forbidden')
    return path


def _read_plain(path, maximum):
    path = _plain_chain(path)
    require(path.is_file() and path.stat().st_size <= maximum, 'Missing or oversized regular file')
    with path.open('rb') as stream:
        raw = stream.read(maximum + 1)
    require(len(raw) <= maximum, 'File grew beyond bound')
    return raw


def safe_name(name):
    require(type(name) is str and 0 < len(name) <= 240 and '\\' not in name, 'Invalid archive path')
    parts = name.split('/')
    for part in parts:
        require(TOKEN.fullmatch(part) and not part.endswith('.') and part not in ('.', '..'), 'Unsafe path segment')
        require(part.split('.')[0].casefold() not in RESERVED, 'Windows reserved filename')
    return parts


def _code_path(name, component):
    parts = safe_name(name)
    require(len(parts) >= 3 and parts[:2] == ['payload', component], 'Payload must be inside its component')
    relative = '/'.join(parts[2:])
    immutable = relative in IMMUTABLE_RESOURCES.get(component, set())
    immutable |= component == 'gpu' and bool(re.fullmatch(
        r'EngineRuntime/bin/prime-gap-worker-sm(?:86|120)-k(?:8|16|32|64)[.]exe[.]module[.]json', relative))
    immutable |= component == 'ui' and bool(re.fullmatch(
        r'UpdateRuntime/_internal/[A-Za-z0-9_.-]+[.]dist-info/(?:entry_points[.]txt|top_level[.]txt|licenses/LICENSE(?:[.](?:txt|APACHE|BSD|MIT))?)', relative))
    immutable |= component == 'ui' and bool(re.fullmatch(
        r'UpdateRuntime/_internal/cryptography-50[.]0[.]2[.]dist-info/sboms/(?:cryptography-rust[.]cyclonedx|sbom)[.]json', relative))
    require(immutable or not DATA_PARTS.intersection(p.casefold() for p in parts), 'User data path is forbidden')
    suffix = Path(parts[-1]).suffix.lower()
    documentation = (component == 'ui' and relative.startswith('LICENSES/') or
                     component == 'gpu' and relative.startswith('EngineRuntime/THIRD-PARTY-LICENSES/'))
    documentation &= (suffix in {'', '.txt', '.md', '.markdown-it', '.lib', '.runtime'}
                      or parts[-1].upper().endswith(('-LICENSE', '-COPYING', '-AUTHORS')))
    require(immutable or documentation or suffix in CODE_SUFFIXES or parts[-1] == 'base_library.zip' or not suffix or re.fullmatch(r'.*\.so(?:\.[0-9]+)+', parts[-1]),
            'Only code/resource payloads are accepted; no state/configuration files')


def read_context(path):
    return context_from_bytes(_read_plain(path, MAX_MANIFEST))


def context_from_bytes(raw):
    context = _json(raw)
    _keys(context, 'schema network genesis_hash platform compatibility_set minimum_sequence installed protected_roots')
    require(context['schema'] == 'PGN_COMPONENT_STAGE_CONTEXT_V1', 'Wrong context schema')
    require(context['network'] in ('testnet', 'main') and DIGEST.fullmatch(context['genesis_hash']), 'Invalid chain context')
    require(context['platform'] in ('windows-x86_64', 'linux-x86_64'), 'Unsupported platform')
    require(TOKEN.fullmatch(context['compatibility_set']) and _integer(context['minimum_sequence'], 0, 2**63-1), 'Invalid compatibility context')
    require(type(context['installed']) is list and len(context['installed']) <= 4, 'Invalid installed declarations')
    seen = set()
    for row in context['installed']:
        _keys(row, 'id version abi')
        require(row['id'] in IDS and row['id'] not in seen and TOKEN.fullmatch(row['abi']), 'Invalid installed component')
        _version(row['version'])
        seen.add(row['id'])
    roots = context['protected_roots']
    require(type(roots) is list and 1 <= len(roots) <= 16, 'Explicit protected installation/data roots required')
    for root in roots:
        require(type(root) is str and Path(root).is_absolute(), 'Protected roots must be absolute')
    return context, sha(raw)


def _cache_path(path, context):
    path = _plain_chain(path)
    for root in context['protected_roots']:
        protected = Path(root).resolve()
        require(path != protected and not path.is_relative_to(protected) and not protected.is_relative_to(path),
                'Cache overlaps a protected installation/data directory')
    return path


def init_cache(path, context):
    path = _cache_path(path, context)
    require(not path.exists(), 'A new dedicated cache directory is required')
    path.mkdir(mode=0o700)
    try:
        with (path / '.pgn-stage-cache').open('xb') as out:
            out.write(MARKER)
        with (path / '.writer.lock').open('xb') as out:
            out.write(b'1')
    except BaseException:
        (path / '.pgn-stage-cache').unlink(missing_ok=True)
        (path / '.writer.lock').unlink(missing_ok=True)
        path.rmdir()
        raise
    return path


def _cache(path, context):
    path = _cache_path(path, context)
    require(path.is_dir() and _read_plain(path / '.pgn-stage-cache', 64) == MARKER, 'Not an owned staging cache')
    return path


@contextmanager
def owned_lock(cache):
    """One nonblocking OS writer lock, automatically released on process death."""
    lock = _plain_chain(Path(cache) / '.writer.lock')
    with lock.open('a+b') as stream:
        stream.seek(0, os.SEEK_END)
        if stream.tell() == 0:
            stream.write(b'1');stream.flush()
        stream.seek(0)
        if os.name == 'nt':
            import msvcrt
            msvcrt.locking(stream.fileno(), msvcrt.LK_NBLCK, 1)
        else:
            import fcntl
            fcntl.flock(stream.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        try:
            yield
        finally:
            stream.seek(0)
            if os.name == 'nt':
                msvcrt.locking(stream.fileno(), msvcrt.LK_UNLCK, 1)
            else:
                fcntl.flock(stream.fileno(), fcntl.LOCK_UN)


@contextmanager
def _lock(cache):
    # Never remove an opaque lock left by an older helper: its owner is unknown.
    require(not (cache / '.stage-lock').exists(), 'Staging cache is busy; legacy lock requires review')
    with owned_lock(cache):
        pending = [p for p in cache.iterdir() if re.fullmatch(r'\.pending-(?:[0-9a-f]{24}|[0-9a-f]{32})', p.name)]
        require(len(pending) <= 32, 'Too many interrupted staging transactions')
        for path in pending:
            # The OS lock proves there is no other cooperating writer. These
            # are private code-only scratch directories, never live cohorts.
            _remove_pending(path, cache)
        yield


def _cache_bytes(cache):
    total, entries = 0, 0
    for base, dirs, files in os.walk(cache, followlinks=False):
        for name in dirs + files:
            path = Path(base) / name
            require(not _linked(path), 'Linked entry in staging cache')
            entries += 1
            require(entries <= 4096, 'Staging cache entry bound exceeded')
            if path.is_file():
                total += path.stat().st_size
            require(total <= MAX_CACHE, 'Staging cache exceeds 512 MiB')
    return total


def _manifest(raw, context, selected):
    manifest = _json(raw)
    _keys(manifest, 'schema bundle_id sequence release_version network genesis_hash platform compatibility_set components')
    require(manifest['schema'] == 'PGN_COMPONENT_STAGE_BUNDLE_V1' and TOKEN.fullmatch(manifest['bundle_id']), 'Invalid bundle identity')
    _version(manifest['release_version'])
    require(_integer(manifest['sequence'], 1, 2**63-1) and manifest['sequence'] >= context['minimum_sequence'], 'Bundle sequence below local floor')
    for key in ('network', 'genesis_hash', 'platform', 'compatibility_set'):
        require(manifest[key] == context[key], 'Bundle/context mismatch: ' + key)
    require(type(manifest['components']) is list and 1 <= len(manifest['components']) <= 4, 'Invalid component list')
    components, files, aliases = {}, {}, {}
    for row in manifest['components']:
        _keys(row, 'id version abi primary dependencies files')
        component = row['id']
        require(component in IDS and component not in components and TOKEN.fullmatch(row['abi']), 'Duplicate/invalid component')
        _version(row['version'])
        require(type(row['dependencies']) is list and len(row['dependencies']) <= 3, 'Invalid dependencies')
        dependencies = set()
        for dep in row['dependencies']:
            _keys(dep, 'id version abi')
            require(dep['id'] in IDS and dep['id'] != component and dep['id'] not in dependencies and TOKEN.fullmatch(dep['abi']), 'Invalid dependency')
            _version(dep['version'])
            dependencies.add(dep['id'])
        require(type(row['files']) is list and 1 <= len(row['files']) <= MAX_ENTRIES, 'Missing component files')
        for entry in row['files']:
            _keys(entry, 'path bytes sha256')
            name = entry['path']
            _code_path(name, component)
            require(name not in files and _integer(entry['bytes'], 1, MAX_FILE) and DIGEST.fullmatch(entry['sha256']), 'Invalid file declaration')
            parts = name.split('/')
            for i in range(1, len(parts) + 1):
                prefix = '/'.join(parts[:i])
                folded = prefix.casefold()
                require(folded not in aliases or aliases[folded] == prefix, 'Case alias in payload paths')
                aliases[folded] = prefix
            files[name] = entry
        require(row['primary'] in {f['path'] for f in row['files']}, 'Primary executable absent from component manifest')
        require(not any(name.startswith(row['primary'] + '/') for name in files), 'File/directory path collision')
        components[component] = row
    require(len(files) + 1 <= MAX_ENTRIES and sum(f['bytes'] for f in files.values()) <= MAX_CACHE - 1024*1024, 'Bundle payload exceeds bound')
    for name in files:
        require(not any('/'.join(name.split('/')[:i]) in files for i in range(1, len(name.split('/')))), 'File/directory path collision')
    require(selected and set(selected) <= components.keys(), 'Choose explicit available component IDs')
    installed = {r['id']: r for r in context['installed']}
    chosen, visiting = set(), set()

    def visit(component):
        require(component not in visiting, 'Cyclic component dependencies')
        if component in chosen:
            return
        visiting.add(component)
        row = components[component]
        if component in installed:
            known = installed[component]
            require(not _older(row['version'], known['version']), 'Component version would roll back local declaration')
            require(row['version'] != known['version'] or row['abi'] == known['abi'], 'Same version has conflicting ABI')
        for dep in row['dependencies']:
            available = components.get(dep['id'], installed.get(dep['id']))
            require(available is not None and all(available[k] == dep[k] for k in ('version', 'abi')), 'Unsatisfied exact component dependency')
            if dep['id'] in components:
                visit(dep['id'])
        visiting.remove(component)
        chosen.add(component)

    for component in selected:
        visit(component)
    return manifest, components, files, sorted(chosen)


def _cancel(cancelled):
    if cancelled():
        raise Cancelled('Preparation cancelled; no staged plan published')


def _zip_index(archive):
    infos = archive.infolist()
    require(1 <= len(infos) <= MAX_ENTRIES, 'Archive entry bound exceeded')
    indexed = {}
    for info in infos:
        safe_name(info.orig_filename)
        require(info.orig_filename == info.filename and info.filename not in indexed, 'Duplicate/truncated archive path')
        mode = info.external_attr >> 16
        require(not info.is_dir() and stat.S_IFMT(mode) in (0, stat.S_IFREG) and not (info.external_attr & 0x10), 'Archive links/directories/special files are forbidden')
        require(info.create_system in (0, 3) and not (info.flag_bits & 1) and info.compress_type in (zipfile.ZIP_STORED, zipfile.ZIP_DEFLATED), 'Unsupported encrypted/compression/archive entry')
        require(0 < info.file_size <= MAX_FILE and info.file_size <= max(1, info.compress_size) * MAX_RATIO, 'Archive expansion bound exceeded')
        indexed[info.filename] = info
    require('component-bundle.json' in indexed and indexed['component-bundle.json'].file_size <= MAX_MANIFEST, 'Explicit bounded component manifest required')
    return indexed


def _zip_preflight(stream):
    # Bound the central directory BEFORE ZipFile allocates one object per entry.
    stream.seek(0, os.SEEK_END)
    size = stream.tell()
    stream.seek(max(0, size - 65557))
    tail = stream.read(65557)
    offset = tail.rfind(b'PK\x05\x06')
    require(offset >= 0 and len(tail) - offset >= 22, 'Missing ZIP directory')
    signature, disk, cd_disk, on_disk, entries, cd_size, cd_offset, comment = struct.unpack_from('<4s4H2IH', tail, offset)
    require(disk == cd_disk == 0 and on_disk == entries and 1 <= entries <= MAX_ENTRIES, 'ZIP entry bound/multidisk/Zip64 unsupported')
    require(comment <= 1024 and offset + 22 + comment == len(tail), 'Invalid ZIP trailer')
    require(cd_size <= 1024*1024 and cd_offset + cd_size == size - len(tail) + offset, 'ZIP directory size/layout bound exceeded')
    stream.seek(cd_offset)
    directory = stream.read(cd_size)
    cursor, count = 0, 0
    while cursor < len(directory):
        require(directory[cursor:cursor+4] == b'PK\x01\x02' and len(directory)-cursor >= 46, 'Invalid ZIP directory record')
        name, extra, member_comment = struct.unpack_from('<3H', directory, cursor+28)
        require(1 <= name <= 240 and extra <= 4096 and member_comment <= 1024, 'ZIP member metadata bound exceeded')
        cursor += 46 + name + extra + member_comment
        count += 1
        require(count <= MAX_ENTRIES and cursor <= len(directory), 'ZIP entry bound exceeded')
    require(count == entries and cursor == len(directory), 'ZIP directory count mismatch')
    stream.seek(0)


def _file_digest(stream, cancelled):
    stream.seek(0)
    digest = hashlib.sha256()
    total = 0
    while raw := stream.read(CHUNK):
        _cancel(cancelled)
        total += len(raw)
        require(total <= MAX_ARCHIVE, 'Archive exceeds bound')
        digest.update(raw)
    return digest.hexdigest()


def _primary_format(path, platform):
    with path.open('rb') as file:
        header = file.read(64)
        if platform == 'windows-x86_64':
            require(len(header) >= 64 and header[:2] == b'MZ', 'Primary is not a Windows executable')
            offset = int.from_bytes(header[60:64], 'little')
            require(64 <= offset <= min(path.stat().st_size - 6, CHUNK), 'Invalid bounded PE header')
            file.seek(offset)
            require(file.read(6) == b'PE\0\0\x64\x86', 'Primary executable architecture mismatch')
        else:
            require(len(header) >= 20 and header[:6] == b'\x7fELF\x02\x01' and header[18:20] == b'\x3e\x00', 'Primary is not a Linux x86_64 executable')


def _remove_pending(path, cache):
    # Only this transaction's fresh private directory can be removed.
    require(path.parent == cache and re.fullmatch(r'\.pending-(?:[0-9a-f]{24}|[0-9a-f]{32})', path.name), 'Unsafe cleanup target')
    require(_plain_chain(path).parent == cache and not _linked(path), 'Linked cleanup target')
    shutil.rmtree(path)


def _publish_pending(pending, final, cancelled):
    # Windows can briefly deny a directory rename after its last file closed.
    # Retry only its explicit sharing/access errors, with a finite delay; never
    # overwrite an existing stage or fall back to a non-atomic file copy.
    delays = (0.05, 0.1, 0.2, 0.4, 0.8)
    for attempt in range(len(delays)+1):
        _cancel(cancelled)
        require(not os.path.lexists(final), 'Stage publication target already exists')
        _plain_chain(pending)
        try:
            os.rename(pending, final)
            return
        except OSError as error:
            if os.name != 'nt' or getattr(error, 'winerror', None) not in (5, 32, 33) or attempt == len(delays):
                raise
            time.sleep(delays[attempt])


def _plan(manifest, components, files, chosen, selected, archive_sha256, manifest_sha256, context_sha256):
    chosen_files = {f['path'] for c in chosen for f in components[c]['files']}
    return {
        'schema': 'PGN_COMPONENT_STAGE_PLAN_V1', 'state': 'prepared_activation_blocked',
        'bundle_id': manifest['bundle_id'], 'release_version': manifest['release_version'],
        'sequence': manifest['sequence'], 'network': manifest['network'], 'genesis_hash': manifest['genesis_hash'],
        'platform': manifest['platform'], 'compatibility_set': manifest['compatibility_set'],
        'requested': sorted(set(selected)), 'selected_with_dependencies': chosen,
        'components': [{k: components[c][k] for k in ('id', 'version', 'abi', 'primary', 'dependencies')} for c in chosen],
        'files': [files[f] for f in sorted(chosen_files)], 'archive_sha256': archive_sha256,
        'manifest_sha256': manifest_sha256, 'context_sha256': context_sha256,
        'integrity': 'exact local archive and file SHA256; not publisher authentication',
        'publisher_authenticated': False, 'activation_allowed': False,
        'activation_blockers': ['Publisher trust root and verified TUF metadata chain are absent',
                                'This preparation tool has no installation or process-control operation'],
        'rollback_scope': 'Local supplied floor/declarations only; no authenticated freshness or rollback guarantee',
        'write_scope': 'Dedicated staging cache only; no executable launched',
    }


def stage_bundle(archive_path, expected_sha256, context, context_sha256, cache_path, selected, cancelled=lambda: False,progress=None):
    require(DIGEST.fullmatch(expected_sha256) and DIGEST.fullmatch(context_sha256), 'Explicit archive/context SHA256 required')
    source = _plain_chain(archive_path)
    require(source.is_file() and source.stat().st_size <= MAX_ARCHIVE, 'Missing or oversized local archive')
    cache = _cache(cache_path, context)
    require(not source.is_relative_to(cache), 'Source bundle must be outside staging cache')
    pending = None
    with _lock(cache), source.open('rb') as stream:
        try:
            require(_file_digest(stream, cancelled) == expected_sha256, 'Archive SHA256 mismatch')
            _zip_preflight(stream)
            with zipfile.ZipFile(stream) as archive:
                indexed = _zip_index(archive)
                raw = archive.read('component-bundle.json')
                manifest, components, files, chosen = _manifest(raw, context, selected)
                require(set(indexed) == set(files) | {'component-bundle.json'}, 'Archive/manifest file inventory mismatch')
                final = cache / sha(raw)[:20]
                require(not final.exists(), 'This stage already exists; existing stages are never overwritten')
                chosen_files = {f['path'] for c in chosen for f in components[c]['files']}
                plan = _plan(manifest, components, files, chosen, selected, expected_sha256, sha(raw), context_sha256)
                plan_raw = (json.dumps(plan, indent=2, sort_keys=True) + '\n').encode()
                require(len(plan_raw) <= MAX_MANIFEST, 'Stage plan exceeds bound')
                required = sum(files[f]['bytes'] for f in chosen_files) + len(raw) + len(plan_raw)
                require(_cache_bytes(cache) + required <= MAX_CACHE, 'Staging cache quota exceeded')
                require(shutil.disk_usage(cache).free >= required + 16*1024*1024, 'Insufficient staging disk space')
                _cancel(cancelled)
                pending = cache / ('.pending-' + uuid.uuid4().hex[:24])
                pending.mkdir(mode=0o700)
                component_for={entry['path']:identity for identity,component in components.items()for entry in component['files']}
                totals={identity:sum(entry['bytes']for entry in component['files'])for identity,component in components.items()}
                completed={identity:0 for identity in components};all_total=sum(totals.values());all_done=0
                for name, entry in files.items():
                    _cancel(cancelled)
                    require(indexed[name].file_size == entry['bytes'], 'Declared file size mismatch')
                    target = pending / name
                    if name in chosen_files:
                        target.parent.mkdir(parents=True, exist_ok=True)
                    digest, count = hashlib.sha256(), 0
                    output = target.open('xb') if name in chosen_files else None
                    try:
                        with archive.open(indexed[name]) as input_file:
                            while chunk := input_file.read(CHUNK):
                                _cancel(cancelled)
                                count += len(chunk)
                                if progress:
                                    component=component_for[name]
                                    progress('verify',component,completed[component]+count,totals[component])
                                    progress('verify',completed=all_done+count,total=all_total)
                                require(count <= entry['bytes'], 'Payload grew beyond declared size')
                                digest.update(chunk)
                                if output:
                                    output.write(chunk)
                        require(count == entry['bytes'] and digest.hexdigest() == entry['sha256'], 'Payload SHA256 mismatch')
                        if output:
                            output.flush()
                            os.fsync(output.fileno())
                        completed[component_for[name]]+=count;all_done+=count
                    finally:
                        if output:
                            output.close()
                for component in chosen:
                    _primary_format(pending / components[component]['primary'], manifest['platform'])
                for name, data in (('component-bundle.json', raw), ('plan.json', plan_raw)):
                    with (pending / name).open('xb') as output:
                        output.write(data)
                        output.flush()
                        os.fsync(output.fileno())
            require(_file_digest(stream, cancelled) == expected_sha256, 'Source bundle changed during preparation')
            _cancel(cancelled)
            _publish_pending(pending, final, cancelled)
            pending = None
            return final, plan
        except (zipfile.BadZipFile, RuntimeError, EOFError) as exc:
            raise StageError('Invalid or damaged ZIP bundle') from exc
        finally:
            if pending is not None and pending.exists():
                _remove_pending(pending, cache)


def verify_stage(stage_path, expected_manifest_sha256, context, context_sha256):
    require(DIGEST.fullmatch(expected_manifest_sha256), 'Explicit manifest SHA256 required')
    folder = _plain_chain(stage_path)
    cache = _cache(folder.parent, context)
    require(folder.name in (expected_manifest_sha256, expected_manifest_sha256[:20]) and folder.is_dir(), 'Stage identity mismatch')
    with _lock(cache):
        _cache_bytes(cache)
        raw = _read_plain(folder / 'component-bundle.json', MAX_MANIFEST)
        require(sha(raw) == expected_manifest_sha256, 'Staged manifest SHA256 mismatch')
        plan = _json(_read_plain(folder / 'plan.json', MAX_MANIFEST))
        require(plan.get('activation_allowed') is False and plan.get('publisher_authenticated') is False and plan.get('state') == 'prepared_activation_blocked', 'Stage plan cannot authorize activation')
        require(plan.get('manifest_sha256') == expected_manifest_sha256 and plan.get('context_sha256') == context_sha256, 'Stage/context identity changed')
        manifest, components, files, chosen = _manifest(raw, context, plan.get('requested'))
        expected = {f['path'] for c in chosen for f in components[c]['files']}
        require(type(plan.get('archive_sha256')) is str and DIGEST.fullmatch(plan['archive_sha256']), 'Invalid staged archive digest')
        require(plan == _plan(manifest, components, files, chosen, plan['requested'], plan['archive_sha256'], expected_manifest_sha256, context_sha256), 'Staged selection/plan changed')
        found = set()
        for base, dirs, names in os.walk(folder, followlinks=False):
            for name in dirs + names:
                require(not _linked(Path(base) / name), 'Linked staged entry')
            for name in dirs:
                relative = (Path(base) / name).relative_to(folder).as_posix()
                require(any(f.startswith(relative + '/') for f in expected), 'Unexpected staged directory')
            for name in names:
                path = Path(base) / name
                relative = path.relative_to(folder).as_posix()
                found.add(relative)
                if relative in expected:
                    with path.open('rb') as stream:
                        require(path.stat().st_size == files[relative]['bytes'] and hashlib.file_digest(stream, 'sha256').hexdigest() == files[relative]['sha256'], 'Staged payload changed')
        require(found == expected | {'component-bundle.json', 'plan.json'}, 'Unexpected/missing staged files')
        for component in chosen:
            _primary_format(folder / components[component]['primary'], context['platform'])
        _cache_bytes(cache)
        return plan


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--context', required=True, type=Path)
    sub = parser.add_subparsers(dest='command', required=True)
    init = sub.add_parser('init-cache')
    init.add_argument('cache', type=Path)
    prepare = sub.add_parser('prepare')
    prepare.add_argument('archive', type=Path)
    prepare.add_argument('--sha256', required=True)
    prepare.add_argument('--cache', required=True, type=Path)
    prepare.add_argument('--component', action='append', choices=sorted(IDS), required=True)
    verify = sub.add_parser('verify-stage')
    verify.add_argument('stage', type=Path)
    verify.add_argument('--manifest-sha256', required=True)
    args = parser.parse_args(argv)
    try:
        context, digest = read_context(args.context)
        if args.command == 'init-cache':
            result = {'cache': str(init_cache(args.cache, context)), 'activation_allowed': False}
        elif args.command == 'prepare':
            folder, plan = stage_bundle(args.archive, args.sha256, context, digest, args.cache, args.component)
            result = {'stage': str(folder), 'plan': plan}
        else:
            result = {'verified': True, 'plan': verify_stage(args.stage, args.manifest_sha256, context, digest)}
        print(json.dumps(result, indent=2))
        return 0
    except (StageError, OSError, TypeError, KeyError) as exc:
        print(json.dumps({'prepared': False, 'activation_allowed': False, 'error': str(exc)}), file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        print(json.dumps({'prepared': False, 'activation_allowed': False, 'cancelled': True}), file=sys.stderr)
        return 130


if __name__ == '__main__':
    sys.exit(main())
