"""Bind explicitly declared component versions to immutable build artifacts.

Build specification roots are local inputs and never appear in the public output.
This tool neither infers a version nor downloads, installs or opens wallet data.
"""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import re
import tempfile

VERSION = re.compile(r'^[A-Za-z0-9][A-Za-z0-9.+_-]{0,95}$')
COMPONENTS = {'ui', 'core', 'gpu', 'cpu'}


def relative_code_path(value):
    if not isinstance(value, str) or not value or len(value) > 512 or '\\' in value or ':' in value or '\0' in value:
        raise ValueError('Relative code artifact path required')
    path = PurePosixPath(value)
    if path.is_absolute() or any(part in ('', '.', '..') for part in value.split('/')):
        raise ValueError('Code artifact escapes its installation root')
    lowered = [part.casefold() for part in path.parts]
    if any(part in ('wallets', 'wallet.dat', '.cookie') for part in lowered):
        raise ValueError('Wallet files and credentials are not components')
    if path.suffix.casefold() in ('.sqlite', '.db', '.key', '.log', '.wal', '.shm'):
        raise ValueError('Mutable data is not a code component')
    return path


def build(specification):
    version = specification.get('release_version', '')
    if not isinstance(version, str) or not VERSION.fullmatch(version):
        raise ValueError('Publisher must explicitly declare a bounded release version')
    network = specification.get('network')
    if network not in ('testnet', 'main') or type(specification.get('published')) is not bool:
        raise ValueError('Explicit network and publication status required')
    declarations = specification.get('components')
    if not isinstance(declarations, list) or not 1 <= len(declarations) <= 4:
        raise ValueError('One to four declared components required')
    result, seen = [], set()
    for declaration in declarations:
        identity = declaration.get('id')
        if identity not in COMPONENTS or identity in seen:
            raise ValueError('Unique known component identity required')
        seen.add(identity)
        component_version = declaration.get('version', '')
        if not isinstance(component_version, str) or not VERSION.fullmatch(component_version):
            raise ValueError('Component version cannot be inferred from a filename or digest')
        root = Path(declaration['root']).resolve(strict=True)
        if not root.is_dir() or root == Path(root.anchor):
            raise ValueError('Dedicated code installation root required')
        names = declaration.get('files')
        if not isinstance(names, list) or not 1 <= len(names) <= 512:
            raise ValueError('Bounded explicit artifact list required')
        rows, names_seen, total = [], set(), 0
        for name in names:
            relative = relative_code_path(name)
            if name.casefold() in names_seen:
                raise ValueError('Duplicate code artifact path')
            names_seen.add(name.casefold())
            file = root.joinpath(*relative.parts)
            if file.is_symlink() or not file.resolve(strict=True).is_relative_to(root) or not file.is_file():
                raise ValueError('Code artifact is outside its installation root')
            before = file.stat()
            total += before.st_size
            if before.st_size > 512 * 1024 * 1024 or total > 1024 * 1024 * 1024:
                raise ValueError('Component artifact byte budget exceeded')
            digest = hashlib.sha256()
            with file.open('rb') as stream:
                for chunk in iter(lambda: stream.read(1024 * 1024), b''):
                    digest.update(chunk)
            after = file.stat()
            if (before.st_size, before.st_mtime_ns, before.st_ino) != (after.st_size, after.st_mtime_ns, after.st_ino):
                raise RuntimeError('Artifact changed during identity generation')
            rows.append(dict(path=relative.as_posix(), bytes=after.st_size, sha256=digest.hexdigest()))
        result.append(dict(id=identity, version=component_version, files=rows))
    return dict(schema='PGN_COMPONENT_MANIFEST_V1', release_version=version,
                network=network, published=specification['published'], components=result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--specification', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    if args.specification.stat().st_size > 1024 * 1024:
        raise ValueError('Build specification exceeds its byte budget')
    manifest = build(json.loads(args.specification.read_bytes()))
    encoded = (json.dumps(manifest, indent=2) + '\n').encode()
    if len(encoded) > 2 * 1024 * 1024:
        raise ValueError('Component manifest exceeds its byte budget')
    destination = args.output.resolve()
    handle, temporary = tempfile.mkstemp(prefix='.pgn-components-', dir=destination.parent)
    try:
        with os.fdopen(handle, 'wb') as stream:
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        os.link(temporary, destination)
    finally:
        Path(temporary).unlink(missing_ok=True)
    print(json.dumps(dict(manifest=str(destination), components=len(manifest['components']),
                          source_roots_exported=False, installation_performed=False)))


if __name__ == '__main__':
    main()
