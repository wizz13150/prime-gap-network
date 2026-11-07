"""Closed data/layout tests; no hardware, process control or private wallets."""
import copy
import json
from pathlib import Path
import sys
import unittest
from array import array
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from pgn_composite_witnesses import native_composite_page,DESCRIPTOR_SPAN,TILE_SLOTS,TILES_PER_DESCRIPTOR,CLASSES,MAX_PACKET_BYTES


class CompositeWitnessTests(unittest.TestCase):
    def packet(self, start, length, numbers):
        return dict(schema='affine-composite-witnesses-1',start=str(start),length=str(length),max_a=1024,max_b=16,complete_PSP_coverage=False,
                    events=[[str(n-start),'7',str(n//7)]for n in sorted(set(numbers))])

    def make(self, packet, start, length, job='closed-fixture'):
        return native_composite_page(json.dumps(packet).encode(),job_id=job,start=start,length=length)

    def test_native_layout_and_immutable_upload_for_all_67_to_95_bits(self):
        for bits in range(67,96):
            with self.subTest(bits=bits):
                start=(1<<(bits-1))//30*30;length=DESCRIPTOR_SPAN+128;numbers=[]
                for offset in (0,240,244,247,TILE_SLOTS//8*30-30,TILE_SLOTS//8*30+1,DESCRIPTOR_SPAN-30,DESCRIPTOR_SPAN+2):
                    n=(start+offset+6)//7*7
                    while n%30 not in CLASSES:n+=7
                    numbers.append(n)
                page=self.make(self.packet(start,length,numbers),start,length)
                self.assertEqual(page.event_count,len(set(numbers)));self.assertFalse(page.complete_psp_coverage)
                page.assert_context('closed-fixture',start,length)
                offsets=array('I');offsets.frombytes(page.offsets_le);slots=array('I');slots.frombytes(page.slots_le)
                self.assertEqual(len(offsets),2*TILES_PER_DESCRIPTOR+1)
                observed=set()
                for owner in range(len(offsets)-1):
                    descriptor,tile=divmod(owner,TILES_PER_DESCRIPTOR)
                    for local in slots[offsets[owner]:offsets[owner+1]]:
                        slot=tile*TILE_SLOTS+local
                        observed.add(start+descriptor*DESCRIPTOR_SPAN+(slot//8)*30+CLASSES[slot%8])
                self.assertEqual(observed,set(numbers))
                for n in numbers:
                    descriptor,local=divmod(n-start,DESCRIPTOR_SPAN);slot=(local//30)*8+CLASSES.index(n%30);tile,local=divmod(slot,TILE_SLOTS)
                    masks=page.tile_masks(descriptor,tile);self.assertTrue(masks[local//64]&(1<<(local%64)))
                with self.assertRaises(ValueError):page.assert_context('superseded-job',start,length)
                with self.assertRaises(ValueError):page.tile_masks(2,0)
                self.assertIsInstance(page.offsets_le,bytes);self.assertIsInstance(page.slots_le,bytes)

    def test_known_82_bit_strong_pseudoprime_factor_certificate(self):
        # Published psi_13: this is a composite certificate, not a claim that
        # our bounded affine generator finds every strong pseudoprime.
        n=3317044064679887385961981
        p,q=1287836182261,2575672364521
        self.assertEqual(p*q,n)
        start=n//30*30;length=60
        packet=self.packet(start,length,[])
        packet['events']=[[str(n-start),str(p),str(q)]]
        page=self.make(packet,start,length)
        self.assertEqual(page.event_count,1)
        self.assertFalse(page.complete_psp_coverage)

    def test_brian_actual_large_factor_certificate(self):
        start=160696461657079905840;length=1024;n=160696461657079906273
        packet=self.packet(start,length,[]);packet['events']=[[str(n-start),'2142738313','74995840921']]
        page=self.make(packet,start,length);slot=((n-start)//30)*8+CLASSES.index(n%30)
        self.assertEqual(page.event_count,1);self.assertTrue(page.tile_masks(0,slot//TILE_SLOTS)[slot%TILE_SLOTS//64]&(1<<(slot%64)))

    def test_false_factors_canonicality_and_ownership_fail_closed(self):
        start=160696461657079905840;length=1024
        good=self.packet(start,length,[]);good['events']=[['433','2142738313','74995840921']]
        for mutation in ('factor_one','false_factor','beyond_end','duplicate','unsorted','cohort','coverage','profile','leading_zero','numeric','unknown_field'):
            bad=copy.deepcopy(good)
            if mutation=='factor_one':bad['events'][0][1]='1'
            if mutation=='false_factor':bad['events'][0][2]='74995840923'
            if mutation=='beyond_end':bad['events'][0][0]=str(length)
            if mutation=='duplicate':bad['events']*=2
            if mutation=='unsorted':bad['events'].append(['0','7','1'])
            if mutation=='cohort':bad['start']=str(start+30)
            if mutation=='coverage':bad['complete_PSP_coverage']=True
            if mutation=='profile':bad['max_a']=True
            if mutation=='leading_zero':bad['events'][0][0]='0433'
            if mutation=='numeric':bad['events'][0][1]=2142738313
            if mutation=='unknown_field':bad['command']='not-executable'
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):self.make(bad,start,length)

    def test_packet_bytes_duplicates_range_and_empty_family(self):
        start=160696461657079905840;length=1024;good=self.packet(start,length,[])
        self.assertEqual(self.make(good,start,length).event_count,0)
        for packet in (b' '* (MAX_PACKET_BYTES+1),b'{"schema":1,"schema":2}',b'[]',b'\xff'):
            with self.assertRaises(ValueError):native_composite_page(packet,job_id='closed',start=start,length=length)
        for begin,count in ((start+1,length),(start,0),(start,10**12+1),((1<<95)//30*30,100),(-1,length)):
            with self.assertRaises(ValueError):self.make(good,begin,count)


if __name__=='__main__':unittest.main()
