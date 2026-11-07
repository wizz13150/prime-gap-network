"""Bounded isolated Core lease/conflict qualification, never a live-node test.

Requires an explicit executable hash and a NEW output directory. All keys,
coordination blocks and the single report commitment are synthetic fixtures.
No scientific computation, mock/system-clock change, GPU or GUI is involved.
"""
import argparse
import base64
import concurrent.futures
import datetime as dt
import hashlib
import http.client
import json
import os
from pathlib import Path
import shutil
import socket
import stat
import struct
import subprocess
import time

GENESIS = '74530feede52b8cda4fa19077747deededef0562844762a43cce462efd29102d'
WALLET = 'isolated-lease-lifecycle'
COUNT = 128


class RpcError(RuntimeError):
    pass


def fingerprint(path):
    value = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(65536), b''):
            value.update(block)
    return value.hexdigest()


def save(path, value):
    temporary = path.with_suffix(path.suffix + '.tmp')
    temporary.write_text(json.dumps(value, indent=2) + '\n', encoding='utf8')
    temporary.replace(path)


def run(args):
    core = Path(args.core).resolve(strict=True)
    if fingerprint(core) != args.expected_sha256:
        raise ValueError('Core executable differs from the explicitly selected build')
    output = Path(args.output).absolute()
    parent = output.parent.resolve(strict=True)
    if output.parent != parent or any(p.is_symlink() for p in (parent, *parent.parents)):
        raise ValueError('Explicit unredirected output parent required')
    output.mkdir(exist_ok=False)
    data = output / 'data'
    data.mkdir()
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', 0))
        port = probe.getsockname()[1]
    started = time.monotonic()
    process = None
    phase = dict(writer='root',stage='NEW_ISOLATED_NODE',core=str(core),sha256=args.expected_sha256,
                 datadir=str(data),rpcport=port,rollback='Stop exact owned process; live nodes are never addressed')
    save(output/'INTENT.json',phase)
    result = dict(PASS=False,core_sha256=args.expected_sha256,genesis=GENESIS,
                  identities=COUNT,concurrent_RPC_clients=4,live_nodes_touched=False,
                  scientific_compute_executed=False,synthetic_pending_report_only=True,
                  system_or_mock_clock_changed=False,GPU_used=False,UI_controlled=False)
    rejected = []
    trace = []

    def checkpoint(stage, **fields):
        phase.update(stage=stage,**fields)
        save(output/'INTENT.json',phase)

    def rpc(method, params=None, wallet=False):
        if method != 'stop' and time.monotonic()-started > 180:
            raise TimeoutError('This exact qualification exceeded its 180s execution budget')
        cookie = data/'testnet/.cookie'
        if cookie.stat().st_size > 4096:
            raise ValueError('Isolated cookie size')
        auth = base64.b64encode(cookie.read_bytes().strip()).decode()
        connection = http.client.HTTPConnection('127.0.0.1',port,timeout=20)
        try:
            connection.request('POST','/wallet/'+WALLET if wallet else '/',
                json.dumps(dict(jsonrpc='2.0',id='isolated-lease',method=method,params=params or [])),
                {'Authorization':'Basic '+auth,'Content-Type':'application/json'})
            response=connection.getresponse()
            raw=response.read(4*1024*1024+1)
        finally:
            connection.close()
        if len(raw)>4*1024*1024:
            raise ValueError('RPC response exceeds the fixture budget')
        value=json.loads(raw)
        if value.get('error'):
            raise RpcError(value['error'])
        return value['result']

    def start():
        nonlocal process
        if fingerprint(core)!=args.expected_sha256:
            raise ValueError('Selected Core changed before start')
        command=[str(core),'-testnet=1','-datadir='+str(data),'-server=1','-rpcport='+str(port),
            '-rpcbind=127.0.0.1','-rpcallowip=127.0.0.1','-listen=0','-discover=0','-dnsseed=0',
            '-fixedseeds=0','-listenonion=0','-connect=0','-dbcache=64','-maxmempool=64',
            '-rpcthreads=4','-rpcworkqueue=64']
        with (output/'stdout.log').open('ab') as out,(output/'stderr.log').open('ab') as err:
            process=subprocess.Popen(command,cwd=core.parent,stdout=out,stderr=err,
                                     creationflags=getattr(subprocess,'CREATE_NO_WINDOW',0))
        checkpoint('STARTING',owned_PID=process.pid)
        until=time.monotonic()+30
        while time.monotonic()<until:
            if process.poll() is not None:
                raise RuntimeError('Isolated Core exited during startup')
            try:
                if rpc('getblockhash',[0])==GENESIS:
                    assert rpc('getconnectioncount')==0
                    return
            except (OSError,ValueError,RpcError):
                time.sleep(.1)
        raise TimeoutError('Isolated Core startup did not finish')

    def stop():
        rpc('stop')
        process.wait(timeout=30)

    def append(envelopes=None, current_time=False):
        work=rpc('getpgnbootstraptemplate',[envelopes or []])
        assert work['genesis']==GENESIS and work['nonce_offset']==82 and work['nonce_bytes']==8
        raw=bytearray.fromhex(work['block'])
        assert len(raw)>112 and raw[:112].hex()==work['header']
        parent=rpc('getblockheader',[work['parent']])
        timestamp=max(int(time.time()) if current_time else genesis_time+work['height']*5,parent['mediantime']+1)
        assert timestamp<=int(time.time())+60
        struct.pack_into('<q',raw,68,timestamp)
        target=int(work['numeric_target'],16)
        for nonce in range(1000000):
            struct.pack_into('<Q',raw,82,nonce)
            digest=hashlib.sha256(hashlib.sha256(raw[:112]).digest()).digest()
            if int.from_bytes(digest,'little')<=target:
                break
        else:
            raise RuntimeError('Bounded private coordination header search exhausted')
        block_hash=digest[::-1].hex()
        checkpoint('BEFORE_ISOLATED_SUBMIT',expected_height=work['height'],expected_hash=block_hash,parent=work['parent'])
        assert rpc('submitblock',[raw.hex()]) is None
        assert rpc('getbestblockhash')==block_hash
        trace.append(dict(height=work['height'],hash=block_hash,time=timestamp,envelopes=len(envelopes or [])))
        return block_hash

    def signed(index, address=None):
        address=address or rpc('getnewaddress',['Isolated lease '+str(index),'bech32'],True)
        request=rpc('getpgnticketrequest',[address,address])
        assert request['request_body_version']==2 and request['lease_seconds']==86400
        assert request['deadline_clock']=='median_chain_time'
        signature=rpc('signmessage',[address,request['challenge']],True)
        sealed=rpc('getpgnticketrequest',[address,address,signature])
        return dict(address=address,request=sealed['request_id'],envelope=sealed['envelope'])

    def rows():
        page=rpc('getpgnstate',[0,100])
        all_rows=list(page['leases'])
        for offset in range(100,page['active_lease_count'],100):
            all_rows.extend(rpc('getpgnstate',[offset,100])['leases'])
        assert len(all_rows)==page['active_lease_count']
        return page,all_rows

    def rejected_rpc(label,method,params):
        try:
            rpc(method,params)
        except RpcError as error:
            rejected.append(dict(case=label,error=str(error)))
        else:
            raise AssertionError(label+' unexpectedly accepted')

    try:
        start()
        genesis_time=rpc('getblockheader',[GENESIS])['time']
        assert int(time.time())-genesis_time>25*3600,'A legal historical-day interval is required; no mock clock fallback'
        rpc('createwallet',[WALLET])
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as clients:
            first=list(clients.map(signed,range(COUNT)))
        assert len({item['request'] for item in first})==COUNT
        rejected_rpc('duplicate envelope within proposed block','getpgnbootstraptemplate',[[first[0]['envelope']]*2])
        append([item['envelope'] for item in first])
        for _ in range(8):
            page,initial=rows()
            if len(initial)==COUNT:
                break
            append()
        assert len(initial)==COUNT
        intervals=sorted((int(row['start']),int(row['end'])) for row in initial)
        assert len(set(intervals))==COUNT and all(a[1]<=b[0] for a,b in zip(intervals,intervals[1:]))
        origin,width=int(page['range_origin']),int(page['ticket_width'])
        for row in initial:
            assert int(row['end'])-int(row['start'])==width
            assert row['deadline_clock']=='median_chain_time' and row['deadline_time']-row['issued_time']==86400
            assert int(row['ticket_number'])==(int(row['start'])-origin)//width+1
        rejected_rpc('confirmed request replay','getpgnbootstraptemplate',[[first[0]['envelope']]])
        for _ in range(145):
            append()
        fast_page,fast_rows=rows()
        assert {row['request']:row for row in fast_rows}=={row['request']:row for row in initial}
        checkpoint('FAST_RECORDS_AND_CONCURRENT_UNIQUENESS_PASSED',identities=COUNT)
        synthetic_root=hashlib.sha256(b'SYNTHETIC_PENDING_REPORT_NO_SEARCH_WAS_RUN').hexdigest()
        proposal=rpc('getpgnreportcommit',[first[0]['request'],synthetic_root])
        signature=rpc('signmessage',[first[0]['address'],proposal['challenge']],True)
        sealed=rpc('getpgnreportcommit',[first[0]['request'],synthetic_root,signature])
        append([sealed['envelope']])
        before_expiry=rpc('getbestblockhash')
        first_current_time=None
        for _ in range(14):
            new_hash=append(current_time=True)
            first_current_time=first_current_time or new_hash
            expired_page,expired=rows()
            if len(expired)==1:
                break
        assert len(expired)==1 and expired[0]['request']==first[0]['request']
        assert expired[0]['report_root']==synthetic_root and expired[0]['state']=='submitted_pending_audits'
        assert expired_page['median_time_past']>=max(row['deadline_time'] for row in initial)
        audits=rpc('getpgnauditstate',[0,1,first[0]['request']])
        assert len(audits)==1 and not audits[0]['audited']
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as clients:
            list(clients.map(lambda item:rejected_rpc('expired owner report '+item['request'],
                'getpgnreportcommit',[item['request'],synthetic_root]),first[1:]))
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as clients:
            second=list(clients.map(lambda index:signed(index+COUNT,first[1]['address'] if index==0 else None),range(COUNT)))
        assert not {item['request'] for item in first}&{item['request'] for item in second}
        append([item['envelope'] for item in second])
        for _ in range(8):
            after_page,after=rows()
            if len(after)==COUNT+1:
                break
            append(current_time=True)
        assert len(after)==COUNT+1
        pending=next(row for row in after if row['request']==first[0]['request'])
        assert pending['report_root']==synthetic_root
        after_intervals=sorted((int(row['start']),int(row['end'])) for row in after)
        assert len(set(after_intervals))==COUNT+1 and all(a[1]<=b[0] for a,b in zip(after_intervals,after_intervals[1:]))
        tip=rpc('getbestblockhash')
        rpc('invalidateblock',[first_current_time])
        reverted_page,reverted=rows()
        assert rpc('getbestblockhash')==before_expiry and len(reverted)==COUNT
        assert {row['request'] for row in reverted}=={item['request'] for item in first}
        rpc('reconsiderblock',[first_current_time])
        assert rpc('getbestblockhash')==tip and rows()[1]==after
        assert rpc('verifychain',[4,0]) is True
        stop();start()
        assert rpc('getbestblockhash')==tip and rows()[1]==after and rpc('verifychain',[4,0]) is True
        result.update(PASS=True,active_leases_after=COUNT+1,stale_owner_reports_rejected=COUNT-1,
            request_replay_and_duplicate_block_refused=True,exact_global_ticket_numbers=True,
            disjoint_simultaneous_ranges=True,fast_records_did_not_shorten_24h=True,actual_mtp_expiry=True,
            pending_report_and_audit_retained=True,expired_miner_can_request_new_work=True,
            reorg_restored_original_owners=True,reconsider_and_cold_restart_identical=True,
            verifychain_level4=True,height=after_page['height'],tip=tip,
            minted_atomic=after_page['minted_atomic'])
        save(output/'PRIVATE-LEASE-ROWS.json',dict(initial=initial,expired=expired,after=after,reverted=reverted,rejections=rejected))
    except BaseException as error:
        result['error']=repr(error)
        checkpoint('FAILED',error=repr(error))
    finally:
        if process is not None and process.poll() is None:
            try:
                stop()
                result['clean_stop']=True
            except BaseException as error:
                result['PASS']=False;result['cleanup_error']=repr(error)
                process.terminate();process.wait(timeout=10)
        result['seconds']=round(time.monotonic()-started,3)
        result['updated']=dt.datetime.now().astimezone().isoformat()
        save(output/'BLOCK-TRACE.json',trace)
        if result['PASS']:
            assert data.resolve().parent==output.resolve() and data.name=='data'
            for root,dirs,files in os.walk(data,followlinks=False):
                for name in dirs+files:
                    info=(Path(root)/name).lstat()
                    if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:
                        raise ValueError('Refusing redirected isolated-data cleanup')
            shutil.rmtree(data)
            result['owned_isolated_data_removed']=True
            checkpoint('PASS_AND_STOPPED',owned_PID=None)
        save(output/'RESULT.json',result)
    return result


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--core',required=True)
    parser.add_argument('--expected-sha256',required=True)
    parser.add_argument('--output',required=True)
    result=run(parser.parse_args())
    print(json.dumps(result,sort_keys=True))
    raise SystemExit(0 if result['PASS'] else 1)
