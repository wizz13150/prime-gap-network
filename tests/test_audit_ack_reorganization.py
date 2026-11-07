"""Later reorg of an already acknowledged reveal; no GPU/network workload."""
from pathlib import Path
import copy
import json
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
import sqlite3

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/protocol'),str(ROOT/'src/engine/runtime_host')]
sys.path.insert(0,str(ROOT/'tests'))
from horizon_network_audits import NetworkAudits,save
from audit_publication_chain_fixture import ChainRPC
from horizon_audit_answers import AuditAnswers

class AnswerCatalogueTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.folder=Path(self.temp.name);self.catalog=AuditAnswers(self.folder);self.rpc=ChainRPC()
    def sealed(self,n=1):
        request=f'{n:064x}';name=request+'-0-0'
        answer=dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64)))
        save(self.folder/(name+'.json'),answer);return name,answer
    def test_extension_never_requeues_canonical_acknowledgements(self):
        name,_=self.sealed();self.catalog.register([name]);self.catalog.migrate_step();self.catalog.observe(self.rpc);self.catalog.acknowledge(name)
        self.rpc.extend();self.catalog.observe(self.rpc)
        self.assertEqual(self.catalog.recover([]),[])
        with patch.object(Path,'iterdir',side_effect=AssertionError('completed migration rescanned history')):
            self.catalog.migrate_step()
    def test_large_fork_recovers_in_bounded_batches_and_respects_pending_cap(self):
        names=[self.sealed(n)[0]for n in range(1,91)];self.catalog.register(names);self.catalog.observe(self.rpc)
        for name in names:self.catalog.acknowledge(name)
        self.rpc.fork();self.catalog.observe(self.rpc)
        first=self.catalog.recover([],limit=16);second=self.catalog.recover(first)
        self.assertEqual((len(first),len(second)),(16,64));self.assertTrue(set(first).isdisjoint(second))
        self.assertEqual(self.catalog.recover([f'{n:064x}-0-0'for n in range(10000,14096)]),[])
    def test_sealed_payload_tamper_refused_without_deleting_evidence(self):
        name,answer=self.sealed();self.catalog.register([name]);self.catalog.observe(self.rpc);self.catalog.acknowledge(name)
        answer['answer']['body']['counts']=['999'];save(self.folder/(name+'.json'),answer)
        self.rpc.fork();self.catalog.observe(self.rpc)
        with self.assertRaisesRegex(ValueError,'digest'):self.catalog.recover([])
        self.assertTrue((self.folder/(name+'.json')).is_file())
    def test_network_change_refused_and_waiting_answers_retained(self):
        name,_=self.sealed();self.catalog.register([name]);self.catalog.observe(self.rpc)
        self.rpc.chain[0]='f'*64
        with self.assertRaisesRegex(ValueError,'another network'):self.catalog.observe(self.rpc)
        self.assertEqual(self.catalog.recover([]),[name])
    def test_legacy_sealed_files_migrate_incrementally_and_cold_idempotently(self):
        names=[self.sealed(n)[0]for n in range(1,71)]
        for _ in range(8):self.catalog.migrate_step()
        self.assertEqual(len(self.catalog.recover([])),64)
        reloaded=AuditAnswers(self.folder)
        with patch.object(Path,'iterdir',side_effect=AssertionError('cold resume rescanned completed migration')):
            reloaded.migrate_step()
        with reloaded.database()as db:self.assertEqual(db.execute('SELECT count(*) FROM answers').fetchone()[0],len(names))
    def test_catalogue_full_fails_without_dropping_any_private_answer(self):
        names=[self.sealed(n)[0]for n in range(1,301)];tiny=AuditAnswers(self.folder,max_pages=8)
        with self.assertRaises(sqlite3.DatabaseError):tiny.register(names)
        self.assertLessEqual(tiny.file.stat().st_size,8*4096)
        self.assertTrue(all((self.folder/(name+'.json')).is_file()for name in names))
    def test_identity_and_storage_bounds_are_fail_closed(self):
        with self.assertRaises(ValueError):self.catalog.register(['../escape'])
        name,answer=self.sealed();answer['slot']=1;save(self.folder/(name+'.json'),answer)
        with self.assertRaises(ValueError):self.catalog.register([name])
        name,_=self.sealed(2);(self.folder/(name+'.json')).write_bytes(b' '*262145)
        with self.assertRaises(ValueError):self.catalog.register([name])

class AcknowledgedReorgTests(unittest.TestCase):
    def test_acknowledged_reveal_reappears_after_fork_and_restart(self):
        with tempfile.TemporaryDirectory()as directory:
            worker=SimpleNamespace(root=Path(directory),rpc=ChainRPC());request='a'*64;name=request+'-0-0'
            audits=NetworkAudits(worker)
            answer=dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64)))
            save(audits.folder/(name+'.json'),answer);save(audits.folder/'pending.json',[name])
            committed=dict(details_root='c'*64,jobs=[dict(slot=0,attempt=0,state='committed')])
            terminal=copy.deepcopy(committed);terminal['jobs'][0]['state']='passed'
            calls=[]
            audits.states=lambda requests:{request:copy.deepcopy(committed)}
            audits.state=lambda wanted:copy.deepcopy(terminal)
            audits.apply=lambda *args:(calls.append(args)or dict(available=True))
            audits.reveal_committed()
            self.assertEqual(json.loads((audits.folder/'pending.json').read_bytes()),[])
            worker.rpc.fork()
            recovered=NetworkAudits(worker)
            recovered.states=lambda requests:{request:copy.deepcopy(committed)}
            recovered.state=lambda wanted:copy.deepcopy(terminal)
            recovered.apply=lambda *args:(calls.append(args)or dict(available=True))
            recovered.reveal_committed()
            self.assertEqual(len(calls),2,'Terminal verdict became orphaned: sealed answer must be reindexed, not lost after ACK')
            self.assertEqual(json.loads((recovered.folder/(name+'.json')).read_bytes()),answer)

    def test_crash_after_catalogue_ack_before_legacy_index_write_recovers(self):
        with tempfile.TemporaryDirectory()as directory:
            worker=SimpleNamespace(root=Path(directory),rpc=ChainRPC());request='a'*64;name=request+'-0-0';audits=NetworkAudits(worker)
            answer=dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64)))
            save(audits.folder/(name+'.json'),answer);save(audits.folder/'pending.json',[name])
            committed=dict(details_root='c'*64,jobs=[dict(slot=0,attempt=0,state='committed')]);terminal=copy.deepcopy(committed);terminal['jobs'][0]['state']='passed';calls=[]
            audits.states=lambda requests:{request:copy.deepcopy(committed)};audits.state=lambda wanted:copy.deepcopy(terminal);audits.apply=lambda *args:(calls.append(args)or dict(available=True))
            def crash(file,value):
                if Path(file).name=='pending.json'and value==[]:raise OSError('Injected crash between catalogue ACK and legacy index')
                return save(file,value)
            with patch('horizon_network_audits.save',side_effect=crash):
                with self.assertRaises(OSError):audits.reveal_committed()
            self.assertEqual(json.loads((audits.folder/'pending.json').read_bytes()),[name])
            worker.rpc.fork();audits.reveal_committed();self.assertEqual(len(calls),2)

    def test_racing_tip_cannot_acknowledge_a_terminal_read(self):
        with tempfile.TemporaryDirectory()as directory:
            worker=SimpleNamespace(root=Path(directory),rpc=ChainRPC());request='a'*64;name=request+'-0-0';audits=NetworkAudits(worker)
            save(audits.folder/(name+'.json'),dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64))))
            save(audits.folder/'pending.json',[name]);committed=dict(details_root='c'*64,jobs=[dict(slot=0,attempt=0,state='committed')]);terminal=copy.deepcopy(committed);terminal['jobs'][0]['state']='passed'
            audits.states=lambda requests:{request:committed};audits.state=lambda wanted:terminal
            audits.apply=lambda *args:(worker.rpc.fork()or dict(available=True))
            audits.reveal_committed();self.assertEqual(json.loads((audits.folder/'pending.json').read_bytes()),[name])

    def test_atomic_view_rejects_ABA_branch_race_despite_equal_outer_tip(self):
        with tempfile.TemporaryDirectory()as directory:
            worker=SimpleNamespace(root=Path(directory),rpc=ChainRPC());request='a'*64;name=request+'-0-0';audits=NetworkAudits(worker)
            save(audits.folder/(name+'.json'),dict(request_id=request,slot=0,attempt=0,answer=dict(body=dict(counts=['7'],nonce='b'*64))))
            save(audits.folder/'pending.json',[name]);terminal=dict(details_root='c'*64,jobs=[dict(slot=0,attempt=0,state='passed')])
            audits.states=lambda requests:{request:terminal};worker.rpc.view_tip='f'*64
            audits.reveal_committed();self.assertEqual(json.loads((audits.folder/'pending.json').read_bytes()),[name])
            self.assertEqual(audits.answers.recover([]),[name])

if __name__=='__main__':unittest.main()
