"""Owned reproducible-cache retirement only; no producer execution claim."""
from pathlib import Path
import hashlib
import json
import sys
import tempfile
import unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/p)for p in ['src/engine/runtime_host','src/engine/shared_composite','src/protocol']]
from horizon_preparation_retention import PreparationRetention


class PreparationRetentionTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.root=Path(self.tmp.name)/'owned-cache';self.r=PreparationRetention(self.root)
        self.plan='a'*64;self.job='b'*64
        self.user=Path(self.tmp.name)/'user-results.txt';self.user.write_bytes(b'never touch user results')
    def fixture(self,plan=None,job=None):
        plan=self.plan if plan is None else plan;job=self.job if job is None else job
        p=self.root/'plans'/plan;j=self.root/'preparation/jobs'/job
        p.mkdir(parents=True);(j/'attempt-1/output').mkdir(parents=True)
        (p/'input.json').write_bytes(b'{"controlled_cache_fixture":true}')
        (p/'job.json').write_text(json.dumps(dict(job=job)))
        (j/'scopes.json').write_bytes(b'controlled scopes')
        factor=j/'attempt-1/output/scope-0.factors';factor.write_bytes(b'reproducible factor cache fixture')
        return p,j,factor
    def retire(self,**kwargs):
        return self.r.retire_locked(self.plan,self.job,delete_job=True,metadata=dict(test_fixture=True),**kwargs)
    def test_compact_provenance_precedes_removal_and_user_files_remain(self):
        p,j,f=self.fixture();h=hashlib.sha256(f.read_bytes()).hexdigest()
        record=self.retire()
        self.assertFalse(p.exists()or j.exists()or self.r.intent.exists())
        self.assertFalse(record['raw_factor_bytes_retained']);self.assertEqual(record['files']['job/attempt-1/output/scope-0.factors']['sha256'],h)
        self.assertEqual(self.user.read_bytes(),b'never touch user results')
        self.assertEqual(self.retire(),record)
    def test_all_bounded_cancelled_attempts_can_retire_with_exact_provenance(self):
        p,j,f=self.fixture()
        for number in range(1,65):
            folder=j/('attempt-'+str(number));(folder/'output').mkdir(parents=True,exist_ok=True)
            (folder/'cancelled.json').write_text(json.dumps(dict(controlled_cancelled_fixture=number)))
            (folder/'producer-owner.json').write_text(json.dumps(dict(controlled_owner_fixture=number)))
        record=self.retire()
        self.assertFalse(p.exists()or j.exists())
        self.assertIn('job/attempt-64/cancelled.json',record['files'])
        self.assertEqual(self.user.read_bytes(),b'never touch user results')

    def test_out_of_budget_or_noncanonical_attempt_directory_is_not_deleted(self):
        for index,name in enumerate(('attempt-0','attempt-01','attempt-65')):
            with self.subTest(name=name):
                p,j,f=self.fixture(plan=('%064x'%index),job=('%064x'%(index+10)))
                unknown=j/name;unknown.mkdir()
                with self.assertRaises(ValueError):
                    self.r.retire_locked(p.name,j.name,delete_job=True,metadata={})
                self.assertTrue(f.exists()and unknown.exists())

    def test_shared_source_job_is_retained(self):
        p,j,f=self.fixture()
        self.r.retire_locked(self.plan,self.job,delete_job=False,metadata={})
        self.assertFalse(p.exists());self.assertTrue(f.is_file())
    def test_unexpected_file_present_before_retirement_is_not_treated_as_generated_cache(self):
        p,j,f=self.fixture();unknown=j/'user-added-note.txt';unknown.write_bytes(b'personal note')
        with self.assertRaises(ValueError):self.retire()
        self.assertTrue(f.exists()and(p/'input.json').exists()and unknown.exists())
        self.assertFalse(self.r.intent.exists())
    def test_interrupted_exact_deletion_resumes_from_compact_record(self):
        p,j,f=self.fixture();unlink=Path.unlink;count=[0]
        def interrupted(path,*args,**kwargs):
            if p in path.parents or j in path.parents:
                count[0]+=1
                if count[0]==2:raise OSError('Controlled partial retirement')
            return unlink(path,*args,**kwargs)
        with patch.object(Path,'unlink',new=interrupted):
            with self.assertRaises(OSError):self.retire()
        self.assertTrue(self.r.intent.exists());self.assertTrue((self.r.records/(self.plan+'.json')).exists())
        self.r.recover_locked()
        self.assertFalse(p.exists()or j.exists()or self.r.intent.exists())
    def test_changed_or_unlisted_cache_data_is_not_deleted(self):
        for kind in ('changed','extra-file','extra-directory'):
            with self.subTest(kind=kind),tempfile.TemporaryDirectory()as tmp:
                original_root=self.root;original_r=self.r
                self.root=Path(tmp)/'cache';self.r=PreparationRetention(self.root);p,j,f=self.fixture()
                recover=self.r.recover_locked;before=f.read_bytes();extra=j/'unlisted-user-data'
                def inject():
                    if self.r.intent.exists():
                        if kind=='changed':f.write_bytes(b'changed cache')
                        elif kind=='extra-file':extra.write_bytes(b'unknown')
                        else:extra.mkdir()
                    return recover()
                with patch.object(self.r,'recover_locked',side_effect=inject):
                    with self.assertRaises(ValueError):self.retire()
                self.assertTrue((p/'input.json').exists());self.assertTrue(f.exists())
                if kind=='changed':f.write_bytes(before)
                elif kind=='extra-file':extra.unlink()
                else:extra.rmdir()
                self.r.recover_locked();self.root=original_root;self.r=original_r
    def test_wrong_compact_record_or_escaping_identity_never_deletes(self):
        p,j,f=self.fixture();recover=self.r.recover_locked
        def corrupt():
            if self.r.intent.exists():(self.r.records/(self.plan+'.json')).write_bytes(b'{}')
            return recover()
        with patch.object(self.r,'recover_locked',side_effect=corrupt):
            with self.assertRaises(ValueError):self.retire()
        self.assertTrue(f.exists()and(p/'input.json').exists())
        with self.assertRaises(ValueError):self.r._targets('../user-results.txt',self.job)
        self.assertTrue(self.user.exists())
    def test_only_terminal_cache_metadata_rotates(self):
        for i in range(3):
            plan='%064x'%i;job='%064x'%(i+100);self.fixture(plan,job)
            self.r.retire_locked(plan,job,delete_job=True,metadata={})
        self.r.rotate_locked(keep=2)
        self.assertEqual(len(list(self.r.records.iterdir())),2)
        self.assertTrue(self.user.exists())


if __name__=='__main__':unittest.main()
