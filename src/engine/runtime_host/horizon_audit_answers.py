"""Bounded recovery catalogue of sealed answers, not a second audit ledger.

The Core remains the verdict authority. A fork rechecks catalogue identities in
small batches; ordinary extension never scans historical answer files. SQLite
stores identities/digests only; private answers remain in their existing files.
"""
from contextlib import contextmanager
from pathlib import Path
import hashlib
import json
import re
import sqlite3
from report_commitment import canonical_bytes

NAME=re.compile(r'([0-9a-f]{64})-([01])-(0|[1-9][0-9]{0,9})\Z')
HASH=re.compile(r'[0-9a-f]{64}\Z')

class AuditAnswers:
    def __init__(self,folder,*,max_pages=32768):
        self.folder=Path(folder);self.file=self.folder/'answers.sqlite3'
        if type(max_pages)is not int or not 8<=max_pages<=32768:raise ValueError('Audit catalogue page budget invalid')
        self.max_pages=max_pages;self._migration=None

    @contextmanager
    def database(self):
        if self.file.exists()and self.file.stat().st_size>self.max_pages*4096:raise BufferError('Audit catalogue exceeds its storage budget')
        db=sqlite3.connect(self.file,timeout=.3)
        try:
            db.execute('PRAGMA page_size=4096')
            db.execute(f'PRAGMA max_page_count={self.max_pages}')
            db.execute('PRAGMA synchronous=FULL')
            db.execute('PRAGMA cache_size=-1024')
            db.executescript('CREATE TABLE IF NOT EXISTS answers(name TEXT PRIMARY KEY,digest TEXT NOT NULL,waiting INTEGER NOT NULL) WITHOUT ROWID; CREATE INDEX IF NOT EXISTS waiting_answers ON answers(waiting,name); CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL) WITHOUT ROWID;')
            with db:yield db
        finally:db.close()

    def read(self,name):
        match=NAME.fullmatch(name)if isinstance(name,str)else None
        if not match or int(match[3])>=2**32:raise ValueError('Invalid sealed audit identity')
        file=self.folder/(name+'.json')
        if file.stat().st_size>262144:raise ValueError('Sealed answer exceeds budget')
        value=json.loads(file.read_bytes())
        if (value.get('request_id')!=match[1]or type(value.get('slot'))is not int or value['slot']!=int(match[2])or type(value.get('attempt'))is not int or value['attempt']!=int(match[3])or not isinstance(value.get('answer'),dict)):
            raise ValueError('Sealed answer identity differs from its file')
        digest=hashlib.sha256(canonical_bytes(value['answer'])).hexdigest()
        return value,digest

    def register(self,names):
        if len(names)>4096:raise ValueError('Audit registration batch exceeds budget')
        rows=[]
        for name in dict.fromkeys(names):
            _,digest=self.read(name);rows.append((name,digest,1))
        with self.database()as db:
            for name,digest,waiting in rows:
                prior=db.execute('SELECT digest FROM answers WHERE name=?',(name,)).fetchone()
                if prior and prior[0]!=digest:raise ValueError('Sealed answer changed after registration')
                db.execute('INSERT OR IGNORE INTO answers VALUES(?,?,?)',(name,digest,waiting))

    def migrate_step(self,limit=16):
        if type(limit)is not int or not 1<=limit<=16:raise ValueError('Audit migration step exceeds budget')
        # Upgrade-only streaming enumeration. Cold restart may revisit names;
        # INSERT OR IGNORE is idempotent and no private payload enters the DB.
        with self.database()as db:
            if db.execute("SELECT 1 FROM meta WHERE key='migration_done'").fetchone():return
        if self._migration is None:self._migration=iter(self.folder.iterdir())
        names=[];done=False
        for _ in range(limit):
            try:file=next(self._migration)
            except StopIteration:done=True;break
            if file.suffix=='.json'and NAME.fullmatch(file.stem):names.append(file.stem)
        self.register(names)
        if done:
            with self.database()as db:db.execute("INSERT OR REPLACE INTO meta VALUES('migration_done','true')")
            self._migration=None

    def restore_pending(self,names):
        """Import legacy window identities without rereading known payloads.

        Every selected answer is still digest-checked by next_page before use.
        register remains strict for new/explicit sealed-answer registration.
        """
        if not isinstance(names,list)or len(names)>4096:raise ValueError('Audit pending import exceeds budget')
        missing=[]
        with self.database()as db:
            for name in dict.fromkeys(names):
                match=NAME.fullmatch(name)if isinstance(name,str)else None
                if not match or int(match[3])>=2**32:raise ValueError('Invalid pending audit identity')
                if db.execute('SELECT 1 FROM answers WHERE name=?',(name,)).fetchone()is None:missing.append(name)
        if missing:self.register(missing)

    def observe(self,rpc):
        from horizon_queue_anchor import observe_queue
        return observe_queue(self.database,rpc,'answers')

    def recover(self,pending,limit=64):
        if type(limit)is not int or not 1<=limit<=64:raise ValueError('Audit recovery step exceeds budget')
        existing=set(pending);room=min(limit,max(0,4096-len(existing)))
        if not room:return []
        with self.database()as db:
            rows=db.execute('SELECT name,digest FROM answers WHERE waiting=1 ORDER BY name LIMIT ?',(len(existing)+room,)).fetchall()
        result=[]
        for name,digest in rows:
            if name in existing:continue
            _,actual=self.read(name)
            if actual!=digest:raise ValueError('Recovered sealed answer digest differs')
            result.append(name)
            if len(result)==room:break
        return result

    def acknowledge(self,name):
        with self.database()as db:
            if db.execute('UPDATE answers SET waiting=0 WHERE name=?',(name,)).rowcount!=1:
                raise ValueError('Unregistered audit acknowledgement')

    def next_page(self,limit=64):
        """Fair bounded work window over the durable waiting catalogue.

        Pending answers are not removed or expired by scheduling. A validated
        cursor wraps lexically, so withheld owners cannot pin the first page.
        Digest failures leave the cursor unchanged; a crash can delay a page
        until the next rotation but cannot lose its durable waiting rows.
        """
        if type(limit)is not int or not 1<=limit<=64:raise ValueError('Audit page exceeds budget')
        with self.database()as db:
            row=db.execute("SELECT value FROM meta WHERE key='waiting_cursor'").fetchone()
            cursor=row[0]if row else ''
            if cursor and not NAME.fullmatch(cursor):raise ValueError('Invalid audit scheduling cursor')
            rows=db.execute('SELECT name,digest FROM answers WHERE waiting=1 AND name>? ORDER BY name LIMIT ?',(cursor,limit)).fetchall()
            if len(rows)<limit and cursor:
                rows+=db.execute('SELECT name,digest FROM answers WHERE waiting=1 AND name<=? ORDER BY name LIMIT ?',(cursor,limit-len(rows))).fetchall()
        result=[]
        for name,digest in rows:
            payload,actual=self.read(name)
            if actual!=digest:raise ValueError('Scheduled sealed answer digest differs')
            result.append((name,payload))
        if result:
            with self.database()as db:
                db.execute("INSERT OR REPLACE INTO meta VALUES('waiting_cursor',?)",(result[-1][0],))
        return result

    def has_answers(self):
        with self.database()as db:return db.execute('SELECT 1 FROM answers LIMIT 1').fetchone()is not None
