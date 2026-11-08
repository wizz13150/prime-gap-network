"""Coarse shared admission for modern native-output spools, never an ACK.

An unfinished owner is charged its complete 128 MiB possible output. A sealed
owner cannot append again and is charged its actual retained log bytes.
The directory itself is the durable reservation. No PID expiry, timer or absent
controller can release it. This is a per-state-root spool pool, not a quota for
wallets, archives, old stdout.log files, or the whole machine.
"""
from contextlib import contextmanager, nullcontext
from pathlib import Path
import json
import os
import re
import stat

OWNER_CAPACITY = 128 * 1024 * 1024
POOL_CAPACITY = 2 * 1024 * 1024 * 1024
MAXIMUM_ENTRIES = 200000


class OutputCapacityExceeded(BufferError):
    code = 'NATIVE_OUTPUT_POOL_FULL'


class OutputCapacityBusy(RuntimeError):
    code = 'NATIVE_OUTPUT_POOL_BUSY'


def _plain(path, directory=False):
    value = path.lstat()
    if (stat.S_ISLNK(value.st_mode)
            or getattr(value, 'st_file_attributes', 0) & 0x400
            or not (stat.S_ISDIR(value.st_mode) if directory else stat.S_ISREG(value.st_mode))):
        raise ValueError('Native output capacity path is redirected or unsupported')


def scope(output, token):
    """Only the exact normal Horizon namespace opts into automatic admission."""
    output = Path(output).absolute()
    owner, ticket = output.parent, output.parent.parent
    tickets = ticket.parent
    if (output.name != 'output' or owner.name != 'native-owner-' + token
            or not re.fullmatch('[0-9a-f]{64}', ticket.name) or tickets.name != 'tickets'):
        return None
    for path in (owner, ticket, tickets, tickets.parent):
        _plain(path, directory=True)
    return tickets


def _reserved_bytes(tickets):
    entries = 0
    reserved = 0

    def bounded(directory):
        nonlocal entries
        with os.scandir(directory) as items:
            for item in items:
                entries += 1
                if entries > MAXIMUM_ENTRIES:
                    raise OutputCapacityExceeded('Native output inventory capacity reached; retained output unchanged')
                yield Path(item.path)

    for ticket in bounded(tickets):
        if not re.fullmatch('[0-9a-f]{64}', ticket.name):
            continue
        _plain(ticket, directory=True)
        for owner in bounded(ticket):
            if not re.fullmatch('native-owner-[0-9a-f]{32}', owner.name):
                continue
            _plain(owner, directory=True)
            output = owner / 'output'
            if not output.exists() and not output.is_symlink():
                continue  # Historical nonsegmented output is outside this pool.
            _plain(output, directory=True)
            index = output / 'index.json'
            finished = False
            if index.exists() or index.is_symlink():
                _plain(index)
                if index.stat().st_size > 65536:
                    raise ValueError('Native output capacity index exceeds metadata budget')
                value = json.loads(index.read_bytes())
                if (not isinstance(value, dict) or value.get('schema') != 'PGN_OUTPUT_SPOOL_V1'
                        or value.get('token') != owner.name[len('native-owner-'):]):
                    raise ValueError('Native output capacity identity changed')
                finished = value.get('finished') is True
            # A missing initial index still reserves maximum future growth.
            # A finished Writer has closed its stream before publishing this
            # seal and refuses further append. Charge every retained byte,
            # including unacknowledged data or files Windows could not unlink.
            retained = 0
            for path in bounded(output):
                if path.name.endswith('.log'):
                    _plain(path)
                    retained += path.stat().st_size
                    if retained > OWNER_CAPACITY:
                        raise OutputCapacityExceeded('Existing native output exceeds its owner capacity; preserve it for recovery')
            reserved += retained if finished else OWNER_CAPACITY
    return reserved


@contextmanager
def _admit(tickets):
    lock = tickets / '.output-capacity.lock'
    if lock.exists() or lock.is_symlink():
        _plain(lock)
    fd = os.open(lock, os.O_RDWR | os.O_CREAT | getattr(os, 'O_NOFOLLOW', 0), 0o600)
    locked = False
    try:
        os.set_inheritable(fd, False)
        if os.fstat(fd).st_size == 0:
            os.write(fd, b'0')
        os.lseek(fd, 0, os.SEEK_SET)
        try:
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(fd, msvcrt.LK_NBLCK, 1)
            else:
                import fcntl
                fcntl.flock(fd, fcntl.LOCK_EX | fcntl.LOCK_NB)
            locked = True
        except OSError as error:
            raise OutputCapacityBusy('Another native output admission is in progress; retry without launching work') from error
        if _reserved_bytes(tickets) + OWNER_CAPACITY > POOL_CAPACITY:
            raise OutputCapacityExceeded('Native output pool is full; preserve pending work and retire acknowledged closed output before retry')
        # The caller creates its directory and publishes its initial index while
        # this lock remains held. A crash after mkdir conservatively keeps credit.
        yield
    finally:
        if locked:
            os.lseek(fd, 0, os.SEEK_SET)
            if os.name == 'nt':
                import msvcrt
                msvcrt.locking(fd, msvcrt.LK_UNLCK, 1)
            else:
                import fcntl
                fcntl.flock(fd, fcntl.LOCK_UN)
        os.close(fd)


def admit(output, token):
    tickets = scope(output, token)
    return nullcontext() if tickets is None else _admit(tickets)
