"""Bounded recovery catalogue of sealed answers, not a second audit ledger.

The Core remains the verdict authority. A fork rechecks catalogue identities in
small batches; ordinary extension never scans historical answer files. SQLite
stores identities/digests only; private answers remain in their existing files.
"""
from contextlib import contextmanager
from pathlib import Path
import hashlib
import json
import os
import re
import sqlite3
import tempfile
import threading
from report_commitment import audit_commitment, canonical_bytes
from pgn_paths import state_path

NAME=re.compile(r'([0-9a-f]{64})-([01])-(0|[1-9][0-9]{0,9})\Z')
HASH=re.compile(r'[0-9a-f]{64}\Z')

class AuditSealRecoveryPending(RuntimeError):
    retryable=True

class AuditSealRecoveryRequired(ValueError):
    retryable=False

class AuditAnswers:
    def __init__(self,folder,*,max_pages=32768):
        self.folder=state_path(folder);self.file=self.folder/'answers.sqlite3'
        if type(max_pages)is not int or not 8<=max_pages<=32768:raise ValueError('Audit catalogue page budget invalid')
        self.max_pages=max_pages;self._migration=None
        self._seal_lock=threading.RLock();self._retained_name=None;self._retained_scan=None
        self.publication_issues=()

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

    @staticmethod
    def checked_name(name):
        match=NAME.fullmatch(name)if isinstance(name,str)else None
        if not match or int(match[3])>=2**32:raise ValueError('Invalid sealed audit identity')
        return match

    def read_path(self,name,file):
        self.checked_name(name)
        with file.open('rb')as stream:raw=stream.read(262145)
        return self.decode(name,raw)

    def decode(self,name,raw):
        match=self.checked_name(name)
        if len(raw)>262144:raise ValueError('Sealed answer exceeds budget')
        value=json.loads(raw)
        if (not isinstance(value,dict)or value.get('request_id')!=match[1]or type(value.get('slot'))is not int or value['slot']!=int(match[2])or type(value.get('attempt'))is not int or value['attempt']!=int(match[3])or not isinstance(value.get('answer'),dict)):
            raise ValueError('Sealed answer identity differs from its file')
        digest=hashlib.sha256(canonical_bytes(value['answer'])).hexdigest()
        return value,digest

    def read(self,name):
        return self.read_path(name,self.folder/(name+'.json'))

    @staticmethod
    def check_binding(value,expected):
        if any(value.get(k)!=expected.get(k)for k in ('request_id','slot','attempt','ticket','report_root','ranges')):
            raise AuditSealRecoveryRequired('Sealed audit replay differs from the reassigned work; retained original must not be used for another generation')
        counts=value.get('counts')
        if (not isinstance(counts,list)or len(counts)!=len(expected['ranges'])
                or any(type(n)is not int or n<0 for n in counts)):
            raise AuditSealRecoveryRequired('Sealed audit replay has incomplete counts')
        prior=expected.get('counts',[])
        if not isinstance(prior,list)or len(prior)>len(counts)or prior!=counts[:len(prior)]:
            raise AuditSealRecoveryRequired('Sealed audit replay does not match its committed counts')
        try:
            answer=audit_commitment(value['ticket'],value['attempt'],[r['index']for r in value['ranges']],
                                    [str(n)for n in counts],value['answer']['body']['nonce'])
        except (KeyError,TypeError,ValueError)as error:
            raise AuditSealRecoveryRequired('Sealed audit replay does not match its committed counts')from error
        if answer!=value['answer']:
            raise AuditSealRecoveryRequired('Sealed audit replay does not match its committed counts')

    def retain_seal(self,name,value,digest):
        """Publish one complete recovery copy without replacing another seal.

        The catalogue's existing answer digest remains the identity authority.
        Publication intents and scheduling metadata are deliberately not frozen.
        """
        self.checked_name(name)
        if hashlib.sha256(canonical_bytes(value['answer'])).hexdigest()!=digest:
            raise AuditSealRecoveryRequired('Sealed answer changed after registration')
        folder=self.folder/'sealed-answers';folder.mkdir(exist_ok=True)
        target=folder/(name+'.json')
        raw=canonical_bytes(value)+b'\n'
        if len(raw)>262144:raise ValueError('Sealed answer exceeds budget')
        temporary=None
        try:
            if not target.exists():
                with tempfile.NamedTemporaryFile(prefix='.seal-',suffix='.tmp',dir=folder,delete=False)as stream:
                    temporary=Path(stream.name);stream.write(raw);stream.flush();os.fsync(stream.fileno())
                try:
                    # Windows rename rejects an existing destination. POSIX
                    # rename would replace it, so retain the no-clobber link.
                    if os.name=='nt':os.rename(temporary,target)
                    else:os.link(temporary,target)
                except FileExistsError:pass
                else:
                    if os.name!='nt':
                        fd=os.open(folder,os.O_RDONLY|os.O_DIRECTORY)
                        try:os.fsync(fd)
                        finally:os.close(fd)
            saved,actual=self.read_path(name,target)
            if actual!=digest:raise AuditSealRecoveryRequired('Sealed answer changed after registration')
            if any(saved.get(k)!=value.get(k)for k in ('request_id','slot','attempt','ticket','report_root','ranges')):
                raise AuditSealRecoveryRequired('Sealed audit replay differs from the reassigned work; immutable generation differs')
            if saved.get('counts')!=value.get('counts'):
                raise AuditSealRecoveryRequired('Sealed audit replay does not match its committed counts')
        finally:
            if temporary is not None:temporary.unlink(missing_ok=True)

    def _recover_retained_step(self,name,digest,expected):
        # Upgrade-only repair, bounded by files AND payload bytes per turn.
        # A process restart may revisit an unfinished scan. Once recovered,
        # this exact identity is an O(1) immutable-copy lookup thereafter.
        folder=self.folder/'retained-assignments'
        if self._retained_name!=name:
            self._retained_name=name;self._retained_scan=iter(folder.iterdir())if folder.is_dir()else iter(())
        for _ in range(16):
            try:file=next(self._retained_scan)
            except StopIteration:
                raise AuditSealRecoveryRequired('Original registered audit seal unavailable; keep the audit folder and restore its original retained answer; nonce regeneration is forbidden')
            if file.suffix!='.json'or not HASH.fullmatch(file.stem)or not file.is_file()or file.stat().st_size>262144:continue
            with file.open('rb')as stream:raw=stream.read(262145)
            if len(raw)>262144:continue
            if hashlib.sha256(raw).hexdigest()!=file.stem:continue
            try:value,actual=self.decode(name,raw)
            except (KeyError,TypeError,ValueError):continue
            if actual!=digest:continue
            self.check_binding(value,expected)
            self.retain_seal(name,value,digest)
            return value
        raise AuditSealRecoveryPending('Computed audit retained; recovering its original sealed answer in bounded archive steps')

    def recover_seal(self,name,expected):
        """Recover before any nonce generation or mutable answer-file write."""
        self.checked_name(name)
        with self._seal_lock:
            with self.database()as db:
                row=db.execute('SELECT digest FROM answers WHERE name=?',(name,)).fetchone()
            digest=row[0]if row else None
            named=self.folder/(name+'.json');immutable=self.folder/'sealed-answers'/(name+'.json')
            sealed=None
            if immutable.is_file():
                sealed,actual=self.read_path(name,immutable)
                if digest is not None and actual!=digest:
                    raise AuditSealRecoveryRequired('Sealed answer changed after registration')
                self.check_binding(sealed,expected)
                # A complete immutable copy may predate the catalogue commit
                # when that first transaction was interrupted.
                if digest is None:digest=actual
            current=None
            if named.is_file():
                try:current,actual=self.read(name)
                except (KeyError,TypeError,ValueError):
                    if digest is None and sealed is None:raise
                else:
                    if digest is None or actual==digest:
                        self.check_binding(current,expected)
                        self.retain_seal(name,current,actual)
                        return current
            if sealed is not None:return sealed
            if digest is None:
                if current is not None:raise AuditSealRecoveryRequired('Unregistered sealed audit is inconsistent; private work retained')
                return None
            return self._recover_retained_step(name,digest,expected)

    def read_registered(self,name,digest):
        try:value,actual=self.read(name)
        except FileNotFoundError:
            file=self.folder/'sealed-answers'/(name+'.json')
            if not file.is_file():raise AuditSealRecoveryRequired('Original registered audit seal unavailable; keep the audit folder and restore its retained answer')
            value,actual=self.read_path(name,file)
            if actual!=digest:raise AuditSealRecoveryRequired('Sealed answer changed after registration')
            if all(k in value for k in ('ticket','report_root','ranges','counts')):self.check_binding(value,value)
            from pgn_session import atomic_bytes
            atomic_bytes(self.folder/(name+'.json'),canonical_bytes(value)+b'\n')
            return value
        if actual==digest:return value
        if not all(k in value for k in ('ticket','report_root','ranges','counts')):
            raise AuditSealRecoveryRequired('Scheduled sealed answer digest differs; original scientific binding required for recovery')
        # Background publication may reach this name before process_one. The
        # same binding/digest checks repair the legacy file; this grants no ACK.
        restored=self.recover_seal(name,value)
        if restored is None:raise AuditSealRecoveryRequired('Original registered audit seal unavailable; private answer retained')
        from pgn_session import atomic_bytes
        atomic_bytes(self.folder/(name+'.json'),canonical_bytes(restored)+b'\n')
        return restored

    def register(self,names):
        if len(names)>4096:raise ValueError('Audit registration batch exceeds budget')
        rows=[]
        for name in dict.fromkeys(names):
            value,digest=self.read(name);rows.append((name,digest,1,value))
        with self._seal_lock:
            with self.database()as db:
                for name,digest,waiting,value in rows:
                    prior=db.execute('SELECT digest FROM answers WHERE name=?',(name,)).fetchone()
                    if prior and prior[0]!=digest:raise ValueError('Sealed answer changed after registration')
                    # The complete copy precedes catalogue publication. A failure
                    # after this link cannot authorize generation of a new nonce.
                    self.retain_seal(name,value,digest)
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
        self.restore_pending(names)
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
            self.read_registered(name,digest)
            result.append(name)
            if len(result)==room:break
        return result

    def acknowledge(self,name):
        with self.database()as db:
            if db.execute('UPDATE answers SET waiting=0 WHERE name=?',(name,)).rowcount!=1:
                raise ValueError('Unregistered audit acknowledgement')

    def next_page(self,limit=64,*,isolate_failures=False):
        """Fair bounded work window over the durable waiting catalogue.

        Pending answers are not removed or expired by scheduling. A validated
        cursor wraps lexically, so withheld owners cannot pin the first page.
        Strict callers leave the cursor unchanged on a digest failure.
        Background publication can isolate a bad retained file while moving
        through unrelated answers. Its waiting row and bytes remain intact.
        """
        if type(limit)is not int or not 1<=limit<=64:raise ValueError('Audit page exceeds budget')
        if type(isolate_failures)is not bool:raise ValueError('Explicit publication isolation choice required')
        self.publication_issues=()
        with self.database()as db:
            row=db.execute("SELECT value FROM meta WHERE key='waiting_cursor'").fetchone()
            cursor=row[0]if row else ''
            if cursor and not NAME.fullmatch(cursor):raise ValueError('Invalid audit scheduling cursor')
            rows=db.execute('SELECT name,digest FROM answers WHERE waiting=1 AND name>? ORDER BY name LIMIT ?',(cursor,limit)).fetchall()
            if len(rows)<limit and cursor:
                rows+=db.execute('SELECT name,digest FROM answers WHERE waiting=1 AND name<=? ORDER BY name LIMIT ?',(cursor,limit-len(rows))).fetchall()
        result=[];issues=[]
        for name,digest in rows:
            try:payload=self.read_registered(name,digest)
            except (OSError,ValueError,AuditSealRecoveryPending)as error:
                if not isolate_failures:raise
                # Never regenerate, acknowledge or delete this answer. A later
                # sweep retries the same registered identity and digest.
                issues.append(dict(name=name,error_type=type(error).__name__))
                continue
            result.append((name,payload))
        self.publication_issues=tuple(issues)
        if rows:
            with self.database()as db:
                db.execute("INSERT OR REPLACE INTO meta VALUES('waiting_cursor',?)",(rows[-1][0],))
        return result

    def has_answers(self):
        with self.database()as db:return db.execute('SELECT 1 FROM answers LIMIT 1').fetchone()is not None
