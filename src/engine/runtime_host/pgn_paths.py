"""Native IO paths for mutable state, independent of Windows long-path policy.

This changes only the filesystem spelling. Network, wallet, GPU and ticket
identities, and the directory containing their existing data, stay unchanged.
Executable identities must keep their own canonical process-image spelling.
"""
from pathlib import Path
import ntpath
import os
from urllib.parse import quote


def windows_extended_path(value):
    """Normalize an absolute drive/UNC path before entering the Win32 namespace."""
    value = ntpath.normpath(os.fspath(value))
    if value.startswith('\\\\?\\UNC\\'):
        value = '\\\\' + value[8:]
    elif value.startswith('\\\\?\\'):
        value = value[4:]
    if value.startswith('\\\\.\\'):
        raise ValueError('A filesystem state path cannot name a Windows device')
    drive, tail = ntpath.splitdrive(value)
    if not drive or (not tail.startswith('\\') and not (drive.startswith('\\\\') and not tail)):
        raise ValueError('An absolute filesystem path is required')
    if value.startswith('\\\\'):
        return '\\\\?\\UNC\\' + value[2:]
    return '\\\\?\\' + value


def io_path(value):
    """Keep links visible to callers that reject linked files or directories."""
    if os.name != 'nt':
        return Path(value).absolute()
    # Prefix before resolve/stat/mkdir: these must also work without opt-in.
    return Path(windows_extended_path(os.path.abspath(value)))


def state_path(value):
    return io_path(value).resolve()


def readonly_sqlite_uri(value):
    # Path.as_uri() treats the '?' in \\?\ as a URI authority. Encode the
    # native spelling as the filename instead; SQLite decodes it before VFS IO.
    # mode=ro prevents a race with deletion from creating a replacement DB.
    return 'file:' + quote(str(state_path(value)), safe='/:') + '?mode=ro'
