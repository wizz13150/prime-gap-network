"""Durable pre-seal work, independent of GPU execution and network submission.

The supplied finalizer owns scientific coverage verification. A pending/failed
finalizer emits no network report. This queue is not a coverage certificate.
One resource coordinator owns the directory, as for ReportOutbox.
"""
from pathlib import Path
import copy,hashlib,json,threading
from pgn_session import atomic_bytes
from report_commitment import canonical_bytes,hash_text,validate_computed_report,verify_report_reveal

MAX_FILE_BYTES=98304


def validate_final(original,corrected,work):
    validate_computed_report(corrected,work)
    identity=lambda body:{k:v for k,v in body.items()if k not in('counts','best')}
    if identity(corrected['body'])!=identity(original['body']):
        raise ValueError('Finalization changed the issued report identity or policy')


class PresealQueue:
    def __init__(self,root,maximum=16):
        if type(maximum)is not int or not 1<=maximum<=16:raise ValueError('Pre-seal queue limit invalid')
        self.root=Path(root).resolve();self.maximum=maximum
        self.file=self.root/'preseal-queue.json'
        self.lock=threading.RLock();self.consumer=threading.Lock()

    @staticmethod
    def read(file,limit):
        if (file.is_symlink() or getattr(file.lstat(),'st_file_attributes',0)&0x400
                or file.stat().st_size>limit):raise ValueError('Pre-seal file escaped its byte/path budget')
        with file.open('rb')as stream:raw=stream.read(limit+1)
        if len(raw)>limit:raise ValueError('Pre-seal file grew beyond its byte budget')
        return json.loads(raw)

    def folder(self,ticket):
        hash_text(ticket)
        p=self.root/'tickets'/ticket
        for part in (self.root,p.parent,p):
            if part.exists()and(part.is_symlink()or getattr(part.lstat(),'st_file_attributes',0)&0x400):
                raise ValueError('Pre-seal directory cannot be a reparse point')
        if p.resolve().parent!=self.root/'tickets':raise ValueError('Pre-seal path escaped owned tickets')
        p.mkdir(parents=True,exist_ok=True)
        return p

    def entries(self):
        # The same lock protects atomic replacement by the producer/consumer.
        # Windows can refuse an open during replacement even though every
        # published file is complete; readers must share that serialization.
        with self.lock:
            if not self.file.exists():return []
            rows=self.read(self.file,32768)
            if not isinstance(rows,list)or len(rows)>self.maximum:raise ValueError('Pre-seal queue exceeds item budget')
            ids=set()
            for row in rows:
                if set(row)!={'request','ticket','input_sha256'}:raise ValueError('Malformed pre-seal reference')
                for value in row.values():hash_text(value)
                if row['request']in ids:raise ValueError('Duplicated pre-seal request')
                ids.add(row['request'])
            return rows

    def save(self,file,value,limit):
        raw=canonical_bytes(value)+b'\n'
        if len(raw)>limit:raise BufferError('Pre-seal output exceeds its byte budget')
        atomic_bytes(file,raw)

    def put(self,work,computed):
        validate_computed_report(computed,work);hash_text(work['request_id'])
        with self.lock:
            file=self.folder(work['ticket'])/'preseal-input.json'
            if file.exists():
                prior=self.read(file,MAX_FILE_BYTES)
                final=file.with_name('preseal-final.json')
                if prior!=dict(work=work,computed=computed)and final.exists():
                    saved=self.read(final,MAX_FILE_BYTES)
                    if (prior.get('work')==work and saved.get('computed')==computed
                            and saved.get('input_sha256')==hashlib.sha256(canonical_bytes(prior)).hexdigest()):
                        # A compact completed session returns the corrected
                        # report on restart. Reuse the original queued input;
                        # do not finalize an already corrected report again.
                        validate_final(prior['computed'],computed,work)
                        computed=prior['computed']
        payload=dict(work=work,computed=computed)
        encoded=canonical_bytes(payload)
        if len(encoded)>MAX_FILE_BYTES:raise BufferError('Pre-seal input exceeds capacity')
        entry=dict(request=work['request_id'],ticket=work['ticket'],input_sha256=hashlib.sha256(encoded).hexdigest())
        with self.lock:
            rows=self.entries()
            old=next((r for r in rows if r['request']==entry['request']),None)
            if old and old!=entry:raise ValueError('Pre-seal request changed before finalization')
            if not old and len(rows)>=self.maximum:raise BufferError('Pre-seal queue full; original computed work retained')
            folder=self.folder(entry['ticket']);file=folder/'preseal-input.json'
            if file.exists():
                if self.read(file,MAX_FILE_BYTES)!=payload:raise ValueError('Immutable pre-seal input changed')
            else:self.save(file,payload,MAX_FILE_BYTES)
            # A legacy network commitment cannot be silently replaced.
            if (folder/'network-report.json').exists()and not(folder/'preseal-final.json').exists():
                raise ValueError('Existing sealed report requires explicit migration, not pre-seal repair')
            if not old:rows.append(entry);self.save(self.file,rows,32768)
        return dict(entry)

    def pump(self,finalize,outbox,*,cancelled=None,on_sealed=None):
        """Process one durable input. No queue lock is held during expensive work.

        finalize(work,computed) must refuse until its complete scientific
        contract is satisfied. The output is journalled before outbox sealing;
        a crash/retry reuses that exact output and the outbox's retained nonce.
        """
        if on_sealed is not None and not callable(on_sealed):raise ValueError('A callable durable-handoff observer is required')
        if Path(outbox.root).resolve()!=self.root:raise ValueError('Outbox belongs to another resource')
        if not self.consumer.acquire(blocking=False):return False
        try:
            with self.lock:
                rows=self.entries()
                if not rows:return False
                entry=rows[0];folder=self.folder(entry['ticket'])
                payload=self.read(folder/'preseal-input.json',MAX_FILE_BYTES)
                if hashlib.sha256(canonical_bytes(payload)).hexdigest()!=entry['input_sha256']:
                    raise ValueError('Pre-seal input fingerprint changed')
                work=payload['work'];computed=payload['computed']
                if work['request_id']!=entry['request']or work['ticket']!=entry['ticket']:
                    raise ValueError('Pre-seal work identity changed')
                validate_computed_report(computed,work)
                output=folder/'preseal-final.json'
                retained=self.read(output,MAX_FILE_BYTES)if output.exists()else None
            if cancelled is not None and cancelled():
                raise InterruptedError('Pre-seal work retained for restart')
            if retained is None:
                corrected=finalize(copy.deepcopy(work),copy.deepcopy(computed))
                validate_final(computed,corrected,work)
                retained=dict(input_sha256=entry['input_sha256'],computed=corrected)
                with self.lock:
                    if output.exists():
                        if self.read(output,MAX_FILE_BYTES)!=retained:raise ValueError('Concurrent finalization differs')
                    else:self.save(output,retained,MAX_FILE_BYTES)
            if retained.get('input_sha256')!=entry['input_sha256']:
                raise ValueError('Finalized result belongs to another input')
            corrected=retained['computed'];validate_final(computed,corrected,work)
            if cancelled is not None and cancelled():
                raise InterruptedError('Finalized result retained before network sealing')
            outbox.put(work,corrected)
            # Confirm the actual immutable handoff, not just a callback result.
            sealed=self.read(folder/'network-report.json',65536)
            verify_report_reveal(sealed,sealed['report_root'])
            if sealed['body']['computed']!=corrected['body']or sealed['body']['request_id']!=entry['request']:
                raise ValueError('Sealed handoff differs from finalized report')
            if on_sealed is not None:
                on_sealed(work)
            with self.lock:
                rows=self.entries()
                if entry not in rows:raise ValueError('Pre-seal queue changed before acknowledgement')
                self.save(self.file,[r for r in rows if r!=entry],32768)
            return True
        finally:self.consumer.release()

    def capacity_available(self):
        with self.lock:return len(self.entries())<self.maximum
