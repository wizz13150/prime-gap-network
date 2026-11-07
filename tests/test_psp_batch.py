import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'src/engine/shared_composite'))
import psp_batch as batch
HELPER=Path(sys.argv.pop(1))if len(sys.argv)>1 else None
N=160696461657079906273
P,Q=2142738313,74995840921


def raw(start,rows):
    return b''.join(struct.pack('<QQQ',n-start,p,q)for n,p,q in rows)


class BatchTests(unittest.TestCase):
    def setUp(self):
        self.start=(N-10000)//30*30
        self.rows=[(N,P,Q),(160696461940037605381,7318844671,21956534011),
                   (160696462160626510561,12676610831,12676610831)]
        self.batch=batch.RecoveryBatch(self.start,10**12,raw(self.start,self.rows))

    def test_all_three_products_checked_and_only_real_EJ_PSP_retained(self):
        self.assertEqual(self.batch.source_count,3)
        self.assertEqual(self.batch.pseudoprime_count,1)
        self.assertFalse(self.batch.complete_psp_coverage)
        page=self.batch.pages(self.start,10**12)
        self.assertEqual(len(page),2)
        self.assertEqual([p.witness_count for p in page],[1,0])
        self.assertFalse(any(p.complete_psp_coverage for p in page))

    def test_exact_native_binary_header_and_factor_product(self):
        page=self.batch.pages(self.start,10**12)[0]
        magic,low,high,length,count=struct.unpack_from('<8s4Q',page.data)
        self.assertEqual((magic,low+(high<<64),length,count),(b'PGNCMP01',self.start,batch.PAGE_POSITIONS,1))
        offset,p,ph,q,qh=struct.unpack_from('<5Q',page.data,40)
        self.assertEqual((self.start+offset,p+(ph<<64),q+(qh<<64)),(N,P,Q))

    def test_half_open_owned_edges_and_partial30_prefix(self):
        self.assertEqual(sum(p.witness_count for p in self.batch.pages(N-1,1)),0)
        self.assertEqual(sum(p.witness_count for p in self.batch.pages(N,1)),1)
        self.assertEqual(sum(p.witness_count for p in self.batch.pages(N+1,1)),1)
        # The last case intentionally includes the physical class30 prefix;
        # the unchanged native walker still owns only the requested lowerprimes.
        self.assertEqual(sum(p.witness_count for p in self.batch.pages(N+30,1)),0)

    def test_physical_page_seam_projects_witness_to_exactly_one_page(self):
        start=N-(batch.PAGE_POSITIONS-17)
        self.assertEqual(start%30,0)
        source=batch.RecoveryBatch(start,2*batch.PAGE_POSITIONS,raw(start,[(N,P,Q)]))
        pages=source.pages(start,2*batch.PAGE_POSITIONS)
        self.assertEqual([p.witness_count for p in pages],[1,0])
        self.assertEqual(sum(p.length for p in pages),2*batch.PAGE_POSITIONS)
        self.assertEqual(pages[0].start+pages[0].length,pages[1].start)

    def test_macro_batch_and_maximum_job_are_sparse_and_bounded(self):
        source=batch.RecoveryBatch(self.start,10**15,raw(self.start,self.rows))
        pages=source.pages(self.start,batch.MAX_OWNED_POSITIONS)
        self.assertEqual(len(pages),65)
        self.assertEqual(sum(p.length for p in pages),batch.MAX_OWNED_POSITIONS)
        self.assertEqual(sum(p.witness_count for p in pages),1)
        self.assertLess(sum(len(p.data)for p in pages),3000)

    def test_rejects_entire_batch_on_invalid_input_without_coercion(self):
        bad=[raw(self.start,[(N,P,Q+1)]),raw(self.start,[(N,1,Q)]),
             raw(self.start,[(N,P,Q),(N,P,Q)]),b'x',b'\0'*((batch.MAX_RAW_WITNESSES+1)*24)]
        for data in bad:
            with self.subTest(bytes=len(data)):
                with self.assertRaises(ValueError):batch.RecoveryBatch(self.start,10**12,data)
        for start,length in[(float(self.start),10**12),(self.start,True),(self.start,0),
                              (self.start+1,10**12),(self.start,10**15+1),
                              ((10**21//30)*30,30)]:
            with self.assertRaises(ValueError):batch.RecoveryBatch(start,length,b'')

    def test_uncovered_or_unsupported_owned_requests_refuse(self):
        for start,length in[(self.start-1,2),(self.start+10**12,1),(self.start,0),
                              (self.start,batch.MAX_OWNED_POSITIONS+1),(True,1)]:
            with self.subTest(start=start,length=length):
                with self.assertRaises(ValueError):self.batch.pages(start,length)

    def test_batch_and_pages_cannot_be_mutated_through_their_API(self):
        with self.assertRaises(AttributeError):self.batch._rows=()
        page=self.batch.pages(N,1)[0]
        with self.assertRaises(AttributeError):page.data=b''
        self.assertEqual(self.batch.pseudoprime_count,1)

    @unittest.skipUnless(HELPER is not None,'native helper provided byCTest')
    def test_actual_shared_native_parser_and_bitmap_consumer(self):
        with tempfile.TemporaryDirectory()as tmp:
            path=Path(tmp)/'page.pgncmp';path.write_bytes(self.batch.pages(self.start,10**12)[0].data)
            lo=(N-90)//30*30;length=batch.PAGE_POSITIONS
            commands=f'PAGE {self.start>>64} {self.start&batch.MASK} {length} {path}\nMAP {lo>>64} {lo&batch.MASK} 64\n'
            result=subprocess.run([str(HELPER)],input=commands.encode(),stdout=subprocess.PIPE,stderr=subprocess.PIPE,timeout=10)
            self.assertEqual(result.returncode,0,result.stderr.decode(errors='replace'))
            got=json.loads(result.stdout)
            classes=(1,7,11,13,17,19,23,29)
            expected=[i for i in range(64)if lo+(i//8)*30+classes[i%8]==N]
            self.assertEqual(got,dict(changed=1,marked=expected))


if __name__=='__main__':unittest.main()
