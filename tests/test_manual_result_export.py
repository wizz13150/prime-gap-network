from pathlib import Path
import json,sys,tempfile,unittest,zipfile
from unittest.mock import patch
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'tools'),str(P/'src/engine/runtime_host'),str(P/'src/protocol'),str(P/'tests')]
from test_gap_observation_export import GapObservationExportTests,GENESIS,SECRET
from horizon_result_export import export

class ManualResultExportTests(unittest.TestCase):
 setUp=GapObservationExportTests.setUp
 ticket=GapObservationExportTests.ticket
 write_compact=GapObservationExportTests.write_compact
 def test_exact_csv_export_excludes_wallets_and_audit_partitions(self):
  first,_,_=self.ticket(count=11)
  audit,_,_=self.ticket('2',2**94+10**14+1,count=7);(audit/'network-report.json').unlink()
  result=export(self.root,GENESIS,self.output)
  self.assertEqual(result['tickets'],1)
  with zipfile.ZipFile(self.output)as z:
   m=json.loads(z.read('manifest.json'));self.assertEqual(m['tickets'],1)
   csv=z.read('batch-0001/observations.csv');self.assertEqual(len(csv.splitlines()),12)
   for n in z.namelist():self.assertNotIn(SECRET.encode(),z.read(n))
 def test_large_history_is_batched_without_truncating_to_64(self):
  for i in range(65):
   folder=self.root/'tickets'/f'{i+1:064x}';folder.mkdir(parents=True)
   (folder/'network-report.json').write_bytes(b'{}');(folder/'scientific-gaps.jsonl.gz').write_bytes(b'x')
  def batch(output,folders,genesis):
   with zipfile.ZipFile(output,'w')as z:z.writestr('observations.csv','ticket\n'+'\n'.join(p.name for p in folders)+'\n')
   return dict(records=len(folders))
  with patch('export_gap_observations.create',side_effect=batch):result=export(self.root,GENESIS,self.output)
  self.assertEqual((result['tickets'],result['batches']),(65,2))
  with zipfile.ZipFile(self.output)as z:
   rows=z.read('batch-0001/observations.csv').decode().splitlines()[1:]+z.read('batch-0002/observations.csv').decode().splitlines()[1:]
   self.assertEqual(rows,[f'{i+1:064x}'for i in range(65)])
 def test_missing_legacy_observations_are_explicit(self):
  self.ticket(count=2);old,_,_=self.ticket('2',2**94+10**14+1,count=1)
  (old/'scientific-gaps.jsonl.gz').unlink()
  export(self.root,GENESIS,self.output)
  with zipfile.ZipFile(self.output)as z:
   self.assertEqual(json.loads(z.read('manifest.json'))['unavailable_ticket_ids'],[old.name])
   self.assertEqual(len(z.read('legacy-summary/retained-best.csv').splitlines()),2)
 def test_no_completed_source_creates_no_empty_success_archive(self):
  (self.root/'tickets').mkdir()
  with self.assertRaisesRegex(ValueError,'No completed'):export(self.root,GENESIS,self.output)
  self.assertFalse(self.output.exists())
if __name__=='__main__':unittest.main()
