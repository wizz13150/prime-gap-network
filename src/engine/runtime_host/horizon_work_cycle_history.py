"""Indexed cold cycle metadata. No answers, network verdicts, or wallet secrets.

Eviction commits here before the small hot JSON journal is replaced. A crash
between those writes leaves a redundant copy; the hot journal wins until its
next eviction. Archived mutations use one SQLite transaction. Capacity failure
preserves the hot journal and applies backpressure instead of deleting duties.
"""
from contextlib import contextmanager
from pathlib import Path
import json
import re
import sqlite3
from report_commitment import canonical_bytes


class WorkCycleHistory:
    maximum_pages = 8192  # 32 MiB database, separate from private answer payloads.
    recheck_page = 16

    def __init__(self, folder, genesis, validate, unique_object):
        self.file = Path(folder) / 'history.sqlite3'
        self.genesis, self.validate, self.unique_object = genesis, validate, unique_object

    @contextmanager
    def database(self, *, create=False):
        if not create and not self.file.exists():
            yield None
            return
        if self.file.exists() and self.file.stat().st_size > self.maximum_pages * 4096:
            raise BufferError('Work-cycle history exceeds its storage budget')
        db = sqlite3.connect(self.file, timeout=.2)
        try:
            db.execute('PRAGMA page_size=4096')
            if db.execute('PRAGMA page_size').fetchone()[0] != 4096:
                raise ValueError('Invalid work-cycle history page size')
            db.execute(f'PRAGMA max_page_count={self.maximum_pages}')
            db.execute('PRAGMA journal_mode=DELETE')
            db.execute('PRAGMA synchronous=FULL')
            db.execute('PRAGMA cache_size=-1024')
            db.execute('PRAGMA foreign_keys=ON')
            db.execute('PRAGMA trusted_schema=OFF')
            with db:
                db.execute('BEGIN IMMEDIATE')
                tables = db.execute("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%'").fetchall()
                if not tables:
                    db.execute('CREATE TABLE meta(genesis TEXT NOT NULL,version INTEGER NOT NULL)')
                    db.execute('INSERT INTO meta VALUES(?,1)', (self.genesis,))
                    db.execute('CREATE TABLE cycles(request TEXT PRIMARY KEY,payload BLOB NOT NULL CHECK(length(payload)<=32768),held TEXT NOT NULL CHECK(length(held)<=240)) WITHOUT ROWID')
                    db.execute('CREATE INDEX held_cycles ON cycles(held,request)')
                    db.execute('CREATE TABLE audit_refs(request TEXT NOT NULL,slot INTEGER NOT NULL,attempt INTEGER NOT NULL,cycle TEXT NOT NULL REFERENCES cycles(request) ON DELETE CASCADE,position INTEGER NOT NULL,PRIMARY KEY(request,slot,attempt),UNIQUE(cycle,position)) WITHOUT ROWID')
                if db.execute('SELECT genesis,version FROM meta').fetchall() != [(self.genesis,1)]:
                    raise ValueError('Work-cycle history belongs to another chain or schema')
                # Additive scheduling metadata, separate from the immutable
                # cycle identity/schema. A crash can repeat a page, never erase
                # a duty; cold restarts must not pin the first sixteen holds.
                db.execute("CREATE TABLE IF NOT EXISTS sweep(singleton INTEGER PRIMARY KEY CHECK(singleton=1),cursor TEXT NOT NULL CHECK(length(cursor) IN(0,64)))")
                db.execute("INSERT OR IGNORE INTO sweep VALUES(1,'')")
                yield db
        finally:
            db.close()

    def decode(self, row):
        if row is None:
            return None
        if row[0] is None:
            raise ValueError('Archived work-cycle exceeds its payload budget')
        cycle = json.loads(row[0], object_pairs_hook=self.unique_object)
        self.validate(dict(schema='PGN_WORK_CYCLES_V1',genesis=self.genesis,cycles=[cycle]))
        if cycle['stage'] != 'completed':
            raise ValueError('Only completed cycles can enter cold history')
        return cycle

    def get(self, request):
        with self.database() as db:
            if db is None:
                return None
            cycle = self.decode(db.execute('SELECT CASE WHEN length(payload)<=32768 THEN payload END FROM cycles WHERE request=?',(request,)).fetchone())
            if cycle is not None and cycle['work']['request_id'] != request:
                raise ValueError('Archived work-cycle identity differs from its index')
            return cycle

    @staticmethod
    def check_refs(db, cycle):
        # Canonical Core accounting owns native credit uniqueness, including
        # release/reconsumption after a branch rollback. These are confirmations
        # of a consuming request, not lane assignments to replay locally.
        if 'native_service'in cycle:return
        for position, task in enumerate(cycle['audits']):
            if task and task['stage'] in ('assigned','committed'):
                prior = db.execute('SELECT cycle,position FROM audit_refs WHERE request=? AND slot=? AND attempt=?',(task['request'],task['slot'],task['attempt'])).fetchone()
                if prior is not None and prior != (cycle['work']['request_id'],position):
                    raise ValueError('An audit cannot serve another historical work cycle')

    def check_many(self, cycles):
        with self.database() as db:
            if db is not None:
                for cycle in cycles:
                    self.check_refs(db,cycle)
                    old = self.decode(db.execute('SELECT CASE WHEN length(payload)<=32768 THEN payload END FROM cycles WHERE request=?',
                                                 (cycle['work']['request_id'],)).fetchone())
                    if old is not None and old['work'] != cycle['work']:
                        raise ValueError('Archived assigned work changed during recovery')

    def put(self, cycle):
        # Decode our own encoding too: caller changes must obey the same public
        # shape, stage, budget, and chain rules as a cold read.
        raw = canonical_bytes(cycle)
        self.decode((raw,))
        request = cycle['work']['request_id']
        with self.database(create=True) as db:
            self.check_refs(db,cycle)
            old = self.decode(db.execute('SELECT CASE WHEN length(payload)<=32768 THEN payload END FROM cycles WHERE request=?',(request,)).fetchone())
            if old is not None and old['work'] != cycle['work']:
                raise ValueError('Archived assigned work changed during recovery')
            db.execute('INSERT INTO cycles VALUES(?,?,?) ON CONFLICT(request) DO UPDATE SET payload=excluded.payload,held=excluded.held',
                       (request,raw,cycle['hold']))
            db.execute('DELETE FROM audit_refs WHERE cycle=?',(request,))
            for position,task in enumerate(cycle['audits']):
                if 'native_service'not in cycle and task and task['stage'] in ('assigned','committed'):
                    db.execute('INSERT INTO audit_refs VALUES(?,?,?,?,?)',(task['request'],task['slot'],task['attempt'],request,position))

    def bound(self, request, slot, attempt, position):
        with self.database() as db:
            row = None if db is None else db.execute('SELECT cycle FROM audit_refs WHERE request=? AND slot=? AND attempt=? AND position=?',(request,slot,attempt,position)).fetchone()
            return None if row is None else row[0]

    def held(self, hot_requests, reason):
        # Keyset rotation, not OFFSET or a full history scan. A permanently held
        # first page cannot starve later replays. Hot duplicate copies lose.
        excluded = tuple(hot_requests)
        suffix = (' AND request NOT IN ('+','.join('?' for _ in excluded)+')') if excluded else ''
        with self.database() as db:
            if db is None:
                return []
            cursor = db.execute('SELECT CASE WHEN length(cursor)<=64 THEN cursor END FROM sweep WHERE singleton=1').fetchone()[0]
            if not isinstance(cursor,str) or cursor and re.fullmatch('[0-9a-f]{64}',cursor) is None:
                raise ValueError('Invalid archived work-cycle sweep cursor')
            rows = db.execute('SELECT request FROM cycles WHERE held=? AND request>?'+suffix+' ORDER BY request LIMIT ?',
                              (reason,cursor,*excluded,self.recheck_page)).fetchall()
            if len(rows) < self.recheck_page:
                rows += db.execute('SELECT request FROM cycles WHERE held=? AND request<=?'+suffix+' ORDER BY request LIMIT ?',
                                   (reason,cursor,*excluded,self.recheck_page-len(rows))).fetchall()
            if rows:
                db.execute('UPDATE sweep SET cursor=? WHERE singleton=1',(rows[-1][0],))
            return [row[0] for row in rows]
