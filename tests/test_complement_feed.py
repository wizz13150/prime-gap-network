"""Factor-page geometry, resumed jobs and transactional publication; no GPU."""
from pathlib import Path
import concurrent.futures,hashlib,json,struct,sys,tempfile,unittest
from unittest.mock import patch
ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'src/engine/runtime_host'),str(ROOT/'src/engine/shared_composite')]
import horizon_complement_feed as feed
from horizon_residual_bundle import ResidualBundle
from psp_batch import PAGE_POSITIONS

N=160696461657079906273
P=2142738313
Q=74995840921
START=N-N%30


def fixture(a=START,b=START+2*PAGE_POSITIONS,rows=((N,P,Q),)):
    # Typed bundle fixture only: execution authenticity is tested separately by
    # test_semiprime_producer and the actual-ticket integration receipt.
    return ResidualBundle(10000000,((a,b),),(tuple(rows),),'a'*64,'b'*64,('c'*64,))


def forbidden(_):raise AssertionError('Aligned input must not ask for new proof')


class ComplementFeedTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory();self.addCleanup(self.tmp.cleanup)
        self.cache=Path(self.tmp.name)/'pages'
        self.prepared=feed.prepare(fixture(),0,forbidden)

    def test_full_and_resumed_projection_preserve_native_coordinates(self):
        self.assertEqual(P*Q,N)
        first=self.prepared.batch.pages(START,2*PAGE_POSITIONS)
        self.assertEqual(len(first),2)
        self.assertEqual(first[0].witness_count,1)
        magic,lo,hi,length,count=struct.unpack('<8s4Q',first[0].data[:40])
        self.assertEqual((magic,lo+(hi<<64),length,count),(b'PGNCMP01',START,PAGE_POSITIONS,1))
        self.assertEqual(struct.unpack('<5Q',first[0].data[40:]),(N-START,P,0,Q,0))
        resumed=self.prepared.batch.pages(START+30,PAGE_POSITIONS)
        self.assertEqual(resumed[0].start,START+30)
        self.assertEqual(sum(p.witness_count for p in resumed),0)
        self.assertFalse(self.prepared.complete_psp_coverage)

    def test_duplicates_are_canonical_and_product_corruption_is_rejected(self):
        same=feed.prepare(fixture(rows=((N,P,Q),(N,P,Q))),0,forbidden)
        self.assertEqual(same.batch.source_count,1)
        self.assertEqual(same.fingerprint,self.prepared.fingerprint)
        with self.assertRaises(ValueError):feed.prepare(fixture(rows=((N,P,Q+2),)),0,forbidden)

    def test_unscanned_alignment_cannot_hide_a_known_pseudoprime(self):
        seen=[]
        def exact(n):
            seen.append(n)
            self.assertEqual(n,N)
            return False  # Explicit compositeness witness P*Q above.
        with self.assertRaises(feed.CoverageExtensionPending):
            feed.prepare(fixture(a=N+1,rows=()),0,exact)
        self.assertEqual(seen,[N])
        with self.assertRaises(ValueError):feed.prepare(fixture(a=N+1,rows=()),0,lambda _:1)

    def test_binding_and_domain_changes_do_not_silently_reuse_pages(self):
        changed=fixture();changed=ResidualBundle(changed.cutoff,changed.scopes,changed.witnesses,
            changed.producer_sha256,'d'*64,changed.witness_sha256)
        self.assertNotEqual(feed.prepare(changed,0,forbidden).fingerprint,self.prepared.fingerprint)
        for a,l in [(START-30,30),(START,64000000000001),(START+2*PAGE_POSITIONS,1)]:
            with self.subTest(a=a,l=l),self.assertRaises(ValueError):self.prepared.batch.pages(a,l)
        with self.assertRaises(ValueError):feed.prepare(fixture(),True,forbidden)

    def publish(self):return feed.publish_pages(self.prepared,self.cache,START,2*PAGE_POSITIONS)

    def test_idempotent_publication_and_complete_native_bytes(self):
        folder=self.publish();before={p.name:p.read_bytes()for p in folder.iterdir()}
        self.assertEqual(self.publish(),folder)
        self.assertEqual(before,{p.name:p.read_bytes()for p in folder.iterdir()})
        manifest=json.loads(before['manifest.json'])
        self.assertFalse(manifest['network_admitted'])
        for row in manifest['pages']:
            self.assertEqual(hashlib.sha256(before[row['name']]).hexdigest(),row['sha256'])

    def test_torn_unpublished_write_recovers_without_truncating_evidence(self):
        original=feed._publish_staged
        def torn(path,data):
            with path.open('xb')as stream:stream.write(data[:13])
            raise InterruptedError('simulated process loss before directory commit')
        with patch.object(feed,'_publish_staged',side_effect=torn),self.assertRaises(InterruptedError):self.publish()
        pending=list(self.cache.glob('.pending-*'));self.assertEqual(len(pending),1)
        self.assertFalse(list(self.cache.glob('[0-9a-f]'*64)))
        folder=self.publish();self.assertTrue(folder.is_dir());self.assertFalse(pending[0].exists())
        self.assertEqual((folder/'page-0.pgncmp').read_bytes(),self.prepared.batch.pages(START,2*PAGE_POSITIONS)[0].data)

    def test_corrupt_unpublished_bytes_are_preserved_and_rejected(self):
        def corrupt(path,data):path.write_bytes(b'WRONG');raise InterruptedError()
        with patch.object(feed,'_publish_staged',side_effect=corrupt),self.assertRaises(InterruptedError):self.publish()
        bad=next(self.cache.glob('.pending-*'))/'page-0.pgncmp'
        with self.assertRaises(ValueError):self.publish()
        self.assertEqual(bad.read_bytes(),b'WRONG')

    def test_published_corruption_and_oversize_manifest_are_rejected(self):
        folder=self.publish();page=folder/'page-0.pgncmp';raw=page.read_bytes();page.write_bytes(raw[:-1])
        with self.assertRaises(ValueError):self.publish()
        page.write_bytes(raw);marker=folder/'manifest.json';marker.write_bytes(b'x'*32769)
        with self.assertRaises(ValueError):self.publish()

    def test_quota_never_removes_retained_files_and_allows_exact_resume(self):
        self.cache.mkdir();keep=self.cache/'retained';keep.mkdir();(keep/'proof').write_bytes(b'keep')
        with patch.object(feed,'MAX_CACHE_DIRECTORIES',1),self.assertRaises(BufferError):self.publish()
        self.assertEqual((keep/'proof').read_bytes(),b'keep')
        with patch.object(feed,'MAX_CACHE_BYTES',1),self.assertRaises(BufferError):self.publish()
        self.assertFalse(list(self.cache.glob('.pending-*')))

    def test_two_publishers_share_one_immutable_result(self):
        with concurrent.futures.ThreadPoolExecutor(max_workers=2)as workers:
            values=list(workers.map(lambda _:self.publish(),range(2)))
        self.assertEqual(values[0],values[1]);self.assertFalse(list(self.cache.glob('.pending-*')))


if __name__=='__main__':unittest.main()
