from pathlib import Path
import sys,threading,time,unittest
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'src/engine/runtime_host'))
from horizon_network_lane import NetworkLane

class Tests(unittest.TestCase):
    def test_waiting_audit_and_ticket_precede_background_reentry(self):
        lane=NetworkLane();order=[];lane.acquire();threads=[]
        def work(label):
            with lane:order.append(label)
        try:
            for n in ('audit','ticket'):
                t=threading.Thread(target=work,args=(n,),name=n);t.start();threads.append(t)
                deadline=time.monotonic()+2
                while lane.snapshot()['waiting']<len(threads)and time.monotonic()<deadline:time.sleep(.001)
                self.assertEqual(lane.snapshot()['waiting'],len(threads))
        finally:lane.release()
        with lane:order.append('background')
        for t in threads:t.join(2);self.assertFalse(t.is_alive())
        self.assertEqual(order,['audit','ticket','background'])
    def test_nested_publication_remains_reentrant(self):
        lane=NetworkLane()
        with lane:
            with lane:self.assertEqual(lane.depth,2)
            self.assertEqual(lane.depth,1)
        self.assertEqual(lane.depth,0);self.assertIsNone(lane.owner)
    def test_exception_releases_lane_and_other_thread_cannot_release_it(self):
        lane=NetworkLane();errors=[]
        with self.assertRaises(ValueError):
            with lane:raise ValueError('retained failure')
        with lane:
            def wrong():
                try:lane.release()
                except RuntimeError:errors.append(True)
            t=threading.Thread(target=wrong);t.start();t.join(2)
            self.assertEqual(errors,[True]);self.assertEqual(lane.depth,1)
    def test_contended_transactions_keep_exact_serial_state(self):
        lane=NetworkLane();state=[0];threads=[];barrier=threading.Barrier(8)
        def work():
            barrier.wait(timeout=3)
            for _ in range(1250):
                with lane:
                    prior=state[0]
                    with lane:state[0]=prior+1
        for _ in range(8):
            t=threading.Thread(target=work);t.start();threads.append(t)
        for t in threads:t.join(15);self.assertFalse(t.is_alive())
        self.assertEqual(state[0],10000);self.assertEqual(lane.snapshot()['waiting'],0)
if __name__=='__main__':unittest.main()
