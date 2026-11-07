"""Closed control-file boundaries; no controller, worker, wallet or GPU."""
from pathlib import Path
import json
import sys
import tempfile
import unittest

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from pgn_detached_worker import atomic,load

LIMIT=1024*1024

class OwnerPublicationBudget(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory();self.addCleanup(self.temp.cleanup)
        self.root=Path(self.temp.name);self.file=self.root/'ready.json'

    @staticmethod
    def value(size):
        overhead=len(json.dumps({'data':''}).encode('utf-8'))
        return {'data':'x'*(size-overhead)}

    def test_exact_limit_round_trip(self):
        value=self.value(LIMIT);atomic(self.file,value)
        self.assertEqual(self.file.stat().st_size,LIMIT)
        self.assertEqual(load(self.file),value)

    def test_oversized_write_preserves_committed_receipt(self):
        atomic(self.file,{'token':'previous','closed':True});before=self.file.read_bytes()
        with self.assertRaisesRegex(ValueError,'publication'):
            atomic(self.file,self.value(LIMIT+1))
        self.assertEqual(self.file.read_bytes(),before)
        self.assertFalse(self.file.with_suffix('.json.pending').exists())

    def test_rejected_write_does_not_touch_existing_pending(self):
        pending=self.file.with_suffix('.json.pending');pending.write_bytes(b'prior incomplete publication')
        with self.assertRaisesRegex(ValueError,'publication'):
            atomic(self.file,self.value(LIMIT+1))
        self.assertEqual(pending.read_bytes(),b'prior incomplete publication')
        self.assertFalse(self.file.exists())

    def test_oversized_read_is_rejected_without_modifying_file(self):
        raw=json.dumps(self.value(LIMIT+1)).encode();self.file.write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'publication'):load(self.file)
        self.assertEqual(self.file.read_bytes(),raw)

    def test_reader_counts_utf8_bytes(self):
        raw=json.dumps({'data':'é'*(LIMIT//2)},ensure_ascii=False).encode('utf-8')
        self.assertGreater(len(raw),LIMIT);self.assertLess(len(raw.decode()),LIMIT)
        self.file.write_bytes(raw)
        with self.assertRaisesRegex(ValueError,'publication'):load(self.file)

    def test_malformed_read_then_valid_recovery(self):
        self.file.write_bytes(b'{invalid')
        with self.assertRaises(ValueError):load(self.file)
        atomic(self.file,{'token':'recovered','closed':False})
        self.assertEqual(load(self.file),{'token':'recovered','closed':False})

if __name__=='__main__':unittest.main()
