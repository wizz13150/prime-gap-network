"""Durable owner-report identities with bounded RAM, storage and fair polling.

Scientific bodies stay in their original ticket files. Acknowledged identities
remain available for reorg recovery; only Core can supply the verdict.
"""
from contextlib import contextmanager
from pathlib import Path
import json
import sqlite3
from horizon_queue_anchor import HASH, observe_queue


class OwnerReports:
    application_id = 0x50474E4F  # PGNO, distinct from wallet/science SQLite files.
    def __init__(self, root, *, max_pages=32768):
        self.root = Path(root).resolve()
        self.file = self.root/'owner-reports.sqlite3'
        if type(max_pages) is not int or not 8 <= max_pages <= 32768:
            raise ValueError('Owner report catalogue page budget invalid')
        self.max_pages = max_pages

    @contextmanager
    def database(self):
        if (self.file.is_symlink() or (self.file.exists() and
                (self.file.stat().st_size > self.max_pages*4096
                 or getattr(self.file.lstat(), 'st_file_attributes', 0) & 0x400))):
            raise BufferError('Owner report catalogue exceeds its storage budget')
        existing = self.file.exists() and self.file.stat().st_size != 0
        db = sqlite3.connect(self.file, timeout=.3)
        try:
            if existing and db.execute('PRAGMA application_id').fetchone()[0] != self.application_id:
                raise ValueError('Foreign owner report catalogue refused')
            db.execute('PRAGMA page_size=4096')
            if db.execute('PRAGMA page_size').fetchone()[0] != 4096:
                raise ValueError('Owner report catalogue page size differs')
            if not existing:
                db.execute(f'PRAGMA application_id={self.application_id}')
            db.execute(f'PRAGMA max_page_count={self.max_pages}')
            db.execute('PRAGMA synchronous=FULL')
            db.execute('PRAGMA cache_size=-1024')
            db.executescript('CREATE TABLE IF NOT EXISTS reports(name TEXT PRIMARY KEY,ticket TEXT NOT NULL,root TEXT NOT NULL,waiting INTEGER NOT NULL) WITHOUT ROWID; CREATE INDEX IF NOT EXISTS waiting_reports ON reports(waiting,name); CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL) WITHOUT ROWID;')
            if not existing:
                db.execute("INSERT INTO meta VALUES('gold_publication_version','3')")
            with db:
                yield db
        finally:
            db.close()

    @staticmethod
    def validate(request, item):
        if (not isinstance(request, str) or not HASH.fullmatch(request) or request == '0'*64 or not isinstance(item, dict)
                or set(item) != {'ticket', 'report_root'}
                or any(not isinstance(item[k], str) or not HASH.fullmatch(item[k]) or item[k] == '0'*64 for k in ('ticket', 'report_root'))):
            raise ValueError('Invalid immutable owner report identity')

    def register(self, request, item):
        self.register_batch({request: item})

    def register_batch(self, values):
        if not isinstance(values, dict) or len(values) > 4096:
            raise ValueError('Owner report registration batch exceeds budget')
        for request, item in values.items():
            self.validate(request, item)
        with self.database() as db:
            added = False
            for request, item in values.items():
                prior = db.execute('SELECT ticket,root FROM reports WHERE name=?', (request,)).fetchone()
                if prior and prior != (item['ticket'], item['report_root']):
                    raise ValueError('Owner report commitment changed')
                added |= db.execute('INSERT OR IGNORE INTO reports VALUES(?,?,?,1)', (request, item['ticket'], item['report_root'])).rowcount == 1
            if added:
                generation = self._generation(db)
                if generation == (1 << 64)-1:
                    raise OverflowError('Owner report catalogue generation exhausted')
                db.execute("INSERT OR REPLACE INTO meta VALUES('generation',?)", (str(generation+1),))

    @staticmethod
    def _generation(db):
        row = db.execute("SELECT value FROM meta WHERE key='generation'").fetchone()
        text = row[0] if row else '0'
        if (not isinstance(text, str) or not text.isascii() or not text.isdecimal()
                or len(text) > 20 or str(int(text)) != text or int(text) >= 1 << 64):
            raise ValueError('Invalid owner catalogue generation')
        return int(text)

    def reopen_for_gold_publications(self):
        # V2 acknowledged an entire report after its first Gold. Revisit once
        # after upgrading; immutable report identities and fair cursor survive.
        with self.database() as db:
            row = db.execute("SELECT value FROM meta WHERE key='gold_publication_version'").fetchone()
            if row and row[0] == '3':
                return
            db.execute('UPDATE reports SET waiting=1')
            db.execute("DELETE FROM meta WHERE key='sweep'")
            db.execute("INSERT OR REPLACE INTO meta VALUES('gold_publication_version','3')")

    def invalidate_sweep(self):
        # Keep the fair cursor. A transient RPC/publication race may require
        # revisiting waiting rows even when the tip returns to the same hash.
        with self.database() as db:
            db.execute("DELETE FROM meta WHERE key='sweep'")

    def next_on_anchor(self, anchor, limit=64):
        if (type(limit) is not int or not 1 <= limit <= 64 or not isinstance(anchor, dict)
                or set(anchor) != {'hash', 'height', 'genesis'}
                or type(anchor['height']) is not int or anchor['height'] < 0
                or any(not isinstance(anchor[k], str) or not HASH.fullmatch(anchor[k]) for k in ('hash', 'genesis'))):
            raise ValueError('Bounded owner sweep anchor required')
        with self.database() as db:
            generation = self._generation(db)
            row = db.execute("SELECT value FROM meta WHERE key='waiting_cursor'").fetchone()
            cursor = row[0] if row else ''
            if cursor and not HASH.fullmatch(cursor):
                raise ValueError('Invalid owner report scheduling cursor')
            row = db.execute("SELECT value FROM meta WHERE key='sweep'").fetchone()
            if row and len(row[0]) > 1024:
                raise ValueError('Owner sweep cursor exceeds budget')
            sweep = json.loads(row[0]) if row else None
            if sweep is not None and (not isinstance(sweep, dict)
                    or set(sweep) != {'anchor', 'generation', 'start', 'scan', 'phase'}
                    or sweep['phase'] not in ('after', 'before', 'done')
                    or any(not isinstance(sweep[k], str) or sweep[k] and not HASH.fullmatch(sweep[k]) for k in ('start', 'scan'))
                    or type(sweep['generation']) is not int or not 0 <= sweep['generation'] < 1 << 64):
                raise ValueError('Invalid owner sweep cursor')
            if sweep is None or sweep['anchor'] != anchor or sweep['generation'] != generation:
                sweep = dict(anchor=anchor, generation=generation, start=cursor, scan=cursor, phase='after')
            rows = []
            for _ in range(2):
                if sweep['phase'] == 'done':
                    break
                if sweep['phase'] == 'after':
                    found = db.execute('SELECT name,ticket,root FROM reports WHERE waiting=1 AND name>? ORDER BY name LIMIT ?', (sweep['scan'], limit+1)).fetchall()
                    if len(found) <= limit:
                        sweep['phase'] = 'before' if sweep['start'] else 'done'
                        sweep['scan'] = ''
                else:
                    found = db.execute('SELECT name,ticket,root FROM reports WHERE waiting=1 AND name>? AND name<=? ORDER BY name LIMIT ?', (sweep['scan'], sweep['start'], limit+1)).fetchall()
                    if len(found) <= limit:
                        sweep['phase'] = 'done'
                rows = found[:limit]
                if rows:
                    cursor = rows[-1][0]
                    # A phase transition must start the lower half from zero.
                    if len(found) > limit or sweep['phase'] == 'done':
                        sweep['scan'] = cursor
                    break
            result = []
            for request, ticket, root in rows:
                item = dict(ticket=ticket, report_root=root)
                self.validate(request, item)
                result.append((request, item))
            if result:
                db.execute("INSERT OR REPLACE INTO meta VALUES('waiting_cursor',?)", (cursor,))
            encoded = json.dumps(sweep, separators=(',', ':'))
            if not row or row[0] != encoded:
                db.execute("INSERT OR REPLACE INTO meta VALUES('sweep',?)", (encoded,))
        return result, sweep['phase'] != 'done'

    def import_legacy(self):
        source = self.root/'pending-owner-reports.json'
        if not source.exists():
            return
        if source.is_symlink() or source.stat().st_size > 1024*1024:
            raise ValueError('Owner report legacy index exceeds budget')
        # Remember an exact content digest only. The original index is retained
        # for recovery; it is never the growing queue or deleted by migration.
        import hashlib
        with source.open('rb') as stream:
            raw = stream.read(1024*1024+1)
        if len(raw) > 1024*1024:
            raise ValueError('Owner report legacy index exceeds budget')
        digest = hashlib.sha256(raw).hexdigest()
        with self.database() as db:
            old = db.execute("SELECT value FROM meta WHERE key='legacy_digest'").fetchone()
        if old and old[0] == digest:
            return
        values = json.loads(raw)
        self.register_batch(values)
        with self.database() as db:
            db.execute("INSERT OR REPLACE INTO meta VALUES('legacy_digest',?)", (digest,))

    def observe(self, rpc):
        return observe_queue(self.database, rpc, 'reports')

    def has_reports(self):
        with self.database() as db:
            return db.execute('SELECT 1 FROM reports LIMIT 1').fetchone() is not None

    def retain_publication_turn(self, rows):
        """Persist only deferred identities; never a verdict or a private body."""
        if not isinstance(rows, list) or len(rows) > 64:
            raise ValueError('Owner publication turn exceeds budget')
        seen = set()
        with self.database() as db:
            for request, item in rows:
                self.validate(request, item)
                if request in seen:
                    raise ValueError('Duplicate deferred owner report')
                seen.add(request)
                row = db.execute('SELECT ticket,root,waiting FROM reports WHERE name=?', (request,)).fetchone()
                if row != (item['ticket'], item['report_root'], 1):
                    raise ValueError('Deferred owner differs from retained waiting report')
            if rows:
                db.execute("INSERT OR REPLACE INTO meta VALUES('publication_turn',?)",
                           (json.dumps(rows, separators=(',', ':')),))
            else:
                db.execute("DELETE FROM meta WHERE key='publication_turn'")

    def deferred_publication_turn(self):
        with self.database() as db:
            stored = db.execute("SELECT value FROM meta WHERE key='publication_turn'").fetchone()
            if not stored:
                return []
            if len(stored[0]) > 16384:
                raise ValueError('Deferred owner publication turn exceeds budget')
            rows = json.loads(stored[0])
            if not isinstance(rows, list) or len(rows) > 64:
                raise ValueError('Invalid deferred owner publication turn')
            result, seen = [], set()
            for entry in rows:
                if not isinstance(entry, list) or len(entry) != 2:
                    raise ValueError('Invalid deferred owner identity')
                request, item = entry
                self.validate(request, item)
                if request in seen:
                    raise ValueError('Duplicate deferred owner report')
                seen.add(request)
                row = db.execute('SELECT ticket,root,waiting FROM reports WHERE name=?', (request,)).fetchone()
                if row is None or row[:2] != (item['ticket'], item['report_root']):
                    raise ValueError('Deferred owner differs from immutable report')
                if row[2]:
                    result.append((request, item))
            return result

    def next_page(self, limit=64):
        if type(limit) is not int or not 1 <= limit <= 64:
            raise ValueError('Owner report scheduling page exceeds budget')
        with self.database() as db:
            row = db.execute("SELECT value FROM meta WHERE key='waiting_cursor'").fetchone()
            cursor = row[0] if row else ''
            if cursor and not HASH.fullmatch(cursor):
                raise ValueError('Invalid owner report scheduling cursor')
            rows = db.execute('SELECT name,ticket,root FROM reports WHERE waiting=1 AND name>? ORDER BY name LIMIT ?', (cursor, limit)).fetchall()
            if len(rows) < limit and cursor:
                rows += db.execute('SELECT name,ticket,root FROM reports WHERE waiting=1 AND name<=? ORDER BY name LIMIT ?', (cursor, limit-len(rows))).fetchall()
            result = []
            for request, ticket, root in rows:
                item = dict(ticket=ticket, report_root=root)
                self.validate(request, item)
                result.append((request, item))
            if result:
                db.execute("INSERT OR REPLACE INTO meta VALUES('waiting_cursor',?)", (result[-1][0],))
        return result

    def acknowledge(self, request, item):
        self.validate(request, item)
        with self.database() as db:
            if db.execute('UPDATE reports SET waiting=0 WHERE name=? AND ticket=? AND root=?',
                          (request, item['ticket'], item['report_root'])).rowcount != 1:
                raise ValueError('Unregistered owner report acknowledgement')
