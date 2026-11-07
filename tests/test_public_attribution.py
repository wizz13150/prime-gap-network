"""Immutable attribution, restart/retry and hostile input across the real outbox."""
from pathlib import Path
import concurrent.futures
import copy
import io
import json
import sys
import tempfile
import threading
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/protocol'),str(ROOT/'src/engine/runtime_host')]
from public_attribution import public_name
from horizon_public_profile import PublicProfile
from horizon_report_outbox import ReportOutbox
from report_commitment import seal_report, verify_report_reveal, digest, canonical_bytes


class PublicAttributionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.computed=json.loads((ROOT/'tests/fixtures/network/computed-report.json').read_text())
        cls.work=json.loads((ROOT/'tests/fixtures/network/issued-work.json').read_text())

    def test_unicode_and_utf16_bounds(self):
        for raw,want in [('','PGN'),(' PGN ','PGN'),('E\u0301mile-31','Émile-31'),("Wizz_31. O'Neil","Wizz_31. O'Neil"),('𐐀'*24,'𐐀'*24)]:
            with self.subTest(raw=raw):self.assertEqual(public_name(raw),want)
        for bad in [None,1,[],{},'A'*49,'𐐀'*25,'---','bad\nname','bad<script>','bad\\name','a\u202eb','a\u200bb','a\x00b','a\ud800','A'+'\u0344'*47]:
            with self.subTest(bad=repr(bad)),self.assertRaises(ValueError):public_name(bad)

    def test_legacy_report_is_byte_identical_without_metadata(self):
        sealed=seal_report(self.computed,self.work,nonce='11'*32)
        self.assertNotIn('discoverer',sealed['body'])
        expected=dict(schema='PGN_NETWORK_RANGE_REPORT_V1',genesis=self.work['genesis'],request_id=self.work['request_id'],
            ticket=self.work['ticket'],issue_context=self.work['work_id'],target_numerator=self.work['target_numerator'],
            target_denominator=self.work['target_denominator'],commitment_nonce='11'*32,computed=self.computed['body'])
        self.assertEqual(canonical_bytes(sealed['body']),canonical_bytes(expected))
        self.assertEqual(verify_report_reveal(sealed,sealed['report_root']),expected)

    def test_new_name_binds_root_without_altering_scientific_body(self):
        first=seal_report(self.computed,self.work,nonce='11'*32,discoverer='Alice')
        second=seal_report(self.computed,self.work,nonce='11'*32,discoverer='Bob')
        self.assertNotEqual(first['report_root'],second['report_root'])
        self.assertEqual(first['body']['computed'],self.computed['body'])
        self.assertEqual(second['body']['computed'],first['body']['computed'])
        self.assertFalse(first['scientific_accepted'])
        changed=copy.deepcopy(first);changed['body']['discoverer']['name']='Bob'
        with self.assertRaises(ValueError):verify_report_reveal(changed,first['report_root'])
        self.assertEqual(verify_report_reveal(first,first['report_root'])['discoverer']['name'],'Alice')

    def test_rehashed_hostile_metadata_rejected(self):
        sealed=seal_report(self.computed,self.work,nonce='11'*32,discoverer='Alice')
        for meta in [None,{},dict(schema='wrong',name='Alice'),dict(schema='PGN_PUBLIC_ATTRIBUTION_V1',name='<b>A</b>'),
                     dict(schema='PGN_PUBLIC_ATTRIBUTION_V1',name=' Alice '),dict(schema='PGN_PUBLIC_ATTRIBUTION_V1',name='A',owner='impostor')]:
            changed=copy.deepcopy(sealed);changed['body']['discoverer']=meta
            changed['report_root']=digest(changed['body'])
            with self.subTest(meta=meta),self.assertRaises(ValueError):verify_report_reveal(changed,changed['report_root'])

    def test_save_retry_restart_and_sealed_report_immutability(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);profile=PublicProfile(root)
            self.assertEqual(profile.name(),'PGN')
            profile.update(dict(action='public_profile',name='Alice'))
            box=ReportOutbox(root,discoverer=profile.name)
            (root/'tickets'/self.work['ticket']).mkdir(parents=True)
            first=box.put(self.work,self.computed)
            file=root/'tickets'/self.work['ticket']/'network-report.json';before=file.read_bytes()
            profile.update(dict(action='public_profile',name='Bob'))
            self.assertEqual(box.put(self.work,self.computed),first)
            self.assertEqual(file.read_bytes(),before)
            self.assertEqual(PublicProfile(root).name(),'Bob')
            self.assertEqual(verify_report_reveal(json.loads(before),first['report_root'])['discoverer']['name'],'Alice')
            box.acknowledge(self.work['request_id'],first['report_root'])
            other=copy.deepcopy(self.work);other['ticket']='aa'*32;other['request_id']='bb'*32
            computed=copy.deepcopy(self.computed);computed['body']['ticket_id']=other['ticket'];computed['sha256']=digest(computed['body'])
            (root/'tickets'/other['ticket']).mkdir(parents=True)
            second=box.put(other,computed)
            report=json.loads((root/'tickets'/other['ticket']/'network-report.json').read_bytes())
            self.assertEqual(verify_report_reveal(report,second['report_root'])['discoverer']['name'],'Bob')

    def test_failed_atomic_save_does_not_publish_new_name(self):
        with tempfile.TemporaryDirectory() as folder:
            profile=PublicProfile(folder);profile.update(dict(action='public_profile',name='Alice'))
            before=profile.file.read_bytes()
            with patch('horizon_public_profile.atomic_bytes',side_effect=OSError('disk full')):
                with self.assertRaises(OSError):profile.update(dict(action='public_profile',name='Bob'))
            self.assertEqual(profile.name(),'Alice');self.assertEqual(profile.file.read_bytes(),before)

    def test_parallel_repeated_saves_leave_one_valid_atomic_profile(self):
        with tempfile.TemporaryDirectory() as folder:
            profile=PublicProfile(folder)
            names=['Wizz_'+str(i%5) for i in range(100)]
            with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
                list(pool.map(lambda name:profile.update(dict(action='public_profile',name=name)),names))
            self.assertIn(profile.name(),names);self.assertEqual(PublicProfile(folder).name(),profile.name())
            self.assertLessEqual(profile.file.stat().st_size,512)

    def test_hostile_control_and_oversized_persisted_profile(self):
        with tempfile.TemporaryDirectory() as folder:
            profile=PublicProfile(folder)
            for control in [dict(action='quit',name='Alice'),dict(action='public_profile',name='Alice',enabled=True),dict(action='public_profile',name='a\n')]:
                with self.assertRaises(ValueError):profile.update(control)
                self.assertEqual(profile.name(),'PGN')
            profile.file.write_bytes(b' '*513)
            with self.assertRaises(ValueError):PublicProfile(folder)

    def test_real_control_reader_applies_name_without_changing_mining_state(self):
        from horizon_network_worker import NetworkWorker
        with tempfile.TemporaryDirectory() as folder:
            worker=object.__new__(NetworkWorker);worker.root=Path(folder)
            worker.public_profile=PublicProfile(folder)
            worker.running=threading.Event();worker.running.set();worker.quitting=threading.Event()
            worker.source=object();packets=[]
            worker.emit=lambda packet:packets.append((packet,worker.running.is_set(),worker.quitting.is_set()))
            worker.message=lambda *args:self.fail('Profile error must not change the mining presentation')
            controls=[dict(action='public_profile',name='Alice'),dict(action='public_profile',name='<b>Bob</b>'),dict(action='public_profile',name='Bob')]
            with patch('sys.stdin',io.StringIO(''.join(json.dumps(c)+'\n' for c in controls))):worker.controls()
            self.assertEqual([p[0]['applied'] for p in packets],[True,False,True])
            self.assertTrue(all(running and not quitting for _,running,quitting in packets))
            self.assertEqual(worker.public_name(),'Bob')
            self.assertEqual(PublicProfile(folder).name(),'Bob')
            # Only actual EOF requests a stop, retaining the existing controller contract.
            self.assertFalse(worker.running.is_set());self.assertTrue(worker.quitting.is_set())

if __name__=='__main__':unittest.main()
