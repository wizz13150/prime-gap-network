"""No real keys, Core processes, network or live wallet data in these tests."""
from pathlib import Path
import hashlib,sqlite3,sys,tempfile,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_wallet_epoch import migrate_backup

class EpochMigrationTests(unittest.TestCase):
 def setUp(self):
  self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup);self.root=Path(self.tmp.name)
  self.source=self.root/'backup.dat';self.target=self.root/'migrated.dat'
  c=sqlite3.connect(self.source);c.execute('CREATE TABLE main(key BLOB PRIMARY KEY NOT NULL,value BLOB NOT NULL)')
  c.executemany('INSERT INTO main VALUES(?,?)',[(b'\x03key-inert',b'INERT key fixture'),(b'\x04name-inert',b'fixture label')]);c.execute('PRAGMA application_id=-682340815');c.commit();c.close()
  # Use the canonical network bytes, independently of signed SQL integers.
  self.assertEqual(self.source.read_bytes()[68:72].hex(),'d7544e31')
  self.digest=lambda:hashlib.sha256(self.source.read_bytes()).hexdigest()
 def apply(self,**overrides):
  args=dict(expected_sha256=self.digest(),source_magic='d7544e31',target_magic='d7544e32',source_chain='test',target_chain='test');args.update(overrides)
  return migrate_backup(self.source,self.target,**args)
 def test_copy_preserves_every_record_and_source_while_changing_only_network_identity(self):
  original=self.source.read_bytes();result=self.apply()
  self.assertEqual(self.source.read_bytes(),original);self.assertEqual(self.target.read_bytes()[68:72].hex(),'d7544e32')
  a=sqlite3.connect(self.source);b=sqlite3.connect(self.target)
  try:self.assertEqual(a.execute('SELECT * FROM main ORDER BY key').fetchall(),b.execute('SELECT * FROM main ORDER BY key').fetchall());self.assertEqual(b.execute('PRAGMA integrity_check').fetchone(),('ok',))
  finally:a.close();b.close()
  self.assertTrue(result['records_unchanged']);self.assertTrue(result['source_unchanged']);self.assertFalse(result['Core_acceptance_tested'])
  self.assertEqual(result['records'],2);self.assertEqual(list(self.root.glob('.pgn-wallet-epoch-*')),[])
 def test_existing_destination_is_never_overwritten(self):
  self.target.write_bytes(b'existing wallet');
  with self.assertRaises(ValueError):self.apply()
  self.assertEqual(self.target.read_bytes(),b'existing wallet')
 def test_mainnet_and_other_currency_markers_are_refused(self):
  for changed in (dict(source_chain='main'),dict(target_chain='main'),dict(target_magic='d9b4bef9'),dict(target_magic='d7544e31')):
   with self.subTest(changed=changed),self.assertRaises(ValueError):self.apply(**changed)
  self.assertFalse(self.target.exists())
 def test_wrong_backup_or_network_identity_is_refused(self):
  for changed in (dict(expected_sha256='0'*64),dict(source_magic='d7544e30')):
   with self.subTest(changed=changed),self.assertRaises(ValueError):self.apply(**changed)
  self.assertFalse(self.target.exists())
 def test_live_journal_refused(self):
  Path(str(self.source)+'-wal').write_bytes(b'inert WAL fixture')
  with self.assertRaisesRegex(ValueError,'live wallet'):self.apply()
  self.assertFalse(self.target.exists())
 def test_transaction_records_cannot_be_copied_as_new_epoch_balances(self):
  c=sqlite3.connect(self.source);c.execute('INSERT INTO main VALUES(?,?)',(b'\x02tx'+b'0'*32,b'INERT transaction fixture'));c.commit();c.close()
  with self.assertRaisesRegex(ValueError,'descriptor migration'):self.apply()
  self.assertFalse(self.target.exists());self.assertEqual(list(self.root.glob('.pgn-wallet-epoch-*')),[])
 def test_unexpected_schema_is_refused(self):
  c=sqlite3.connect(self.source);c.execute('CREATE VIEW unexpected AS SELECT * FROM main');c.commit();c.close()
  with self.assertRaisesRegex(ValueError,'schema'):self.apply()
  self.assertFalse(self.target.exists())

if __name__=='__main__':unittest.main()
