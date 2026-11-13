"""An exact completed archive is not an active resident recovery checkpoint."""
from pathlib import Path
import hashlib,json,sys,unittest
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/protocol'),str(ROOT/'src/engine/runtime_host'),str(ROOT/'tests')]
import test_archived_owner_output as fixture
import pgn_output_spool as spool

class ArchivedShutdownTests(fixture.ArchivedOwnerOutputTests):
    def dispatch(self,digest=None):
        cfg=fixture.archive.load(self.two)['configuration']
        identity=dict(pid=71,birth='1234',ticket_id=self.two.name,session_id='c'*32,
                      job_nonce='d'*32,image_sha256='AB'*32,
                      configuration_sha256=digest or hashlib.sha256(fixture.archive.canonical(cfg)).hexdigest())
        path=self.two/'close-identity.json';fixture.save(path,identity)
        binding=spool.commit_job(self.owner,fixture.TOKEN,identity['job_nonce'],2,
                                ['worker.exe','--start',cfg['start']],128,path)
        fixture.save(self.owner/'ready.json',dict(token=fixture.TOKEN,image_sha256='AB'*32,pid=71,birth='1234'))
        spool.dispatch_job(self.owner,binding)

    def test_closed_archive_has_no_new_recovery_checkpoint(self):
        self.dispatch();before=(self.two/'completed-ticket.json.gz').read_bytes()
        self.assertIsNone(spool.bound_job_context(self.owner,fixture.TOKEN))
        self.assertEqual((self.two/'completed-ticket.json.gz').read_bytes(),before)
        self.assertFalse((self.two/'stop.request').exists())

    def test_closed_archive_with_other_configuration_is_refused(self):
        self.dispatch('f'*64)
        with self.assertRaises((spool.SpoolError,ValueError)):spool.bound_job_context(self.owner,fixture.TOKEN)

    def test_corrupt_archive_is_not_a_closed_job_fallback(self):
        self.dispatch();p=self.two/'completed-ticket.json.gz';raw=p.read_bytes();p.write_bytes(raw[:-1]+bytes([raw[-1]^1]))
        with self.assertRaises((spool.SpoolError,ValueError)):spool.bound_job_context(self.owner,fixture.TOKEN)

if __name__=='__main__':unittest.main()
