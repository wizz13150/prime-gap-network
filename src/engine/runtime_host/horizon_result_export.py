"""Manual export of completed search results; no wallet or GPU access."""
from pathlib import Path
import argparse,csv,hashlib,io,json,shutil,sys,tempfile,zipfile
from pgn_paths import state_path
MAX_TICKETS=16384
MAX_OUTPUT=512*1024*1024

def export(root,genesis,output):
    from export_gap_observations import create
    root=state_path(root).resolve(strict=True);folders=[];unavailable=[]
    tickets=root/'tickets'
    if not tickets.is_dir():raise ValueError('No retained search tickets are available')
    for index,folder in enumerate(tickets.iterdir()):
        if index>=MAX_TICKETS:raise ValueError('Export exceeds16384tickets; select a smaller retained data set')
        if folder.is_dir()and not folder.is_symlink()and len(folder.name)==64 and all(c in '0123456789abcdef'for c in folder.name):
            # Assigned audit partitions have no owner search report. Never
            # export another miner's private, not-yet-published audit answers.
            if(folder/'network-report.json').is_file():
                if(folder/'scientific-gaps.jsonl.gz').is_file():folders.append(folder)
                else:unavailable.append(folder.name)
    if not folders:raise ValueError('No completed search result archive is available yet; active tickets are not exported')
    folders.sort(key=lambda p:p.name)
    pages=[]
    with tempfile.TemporaryDirectory(prefix='pgn-results-')as tmp,zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=1)as archive:
        for page,start in enumerate(range(0,len(folders),64),1):
            part=Path(tmp)/'part.zip';result=create(part,folders[start:start+64],genesis)
            with zipfile.ZipFile(part)as source:
                for name in source.namelist():
                    with source.open(name)as incoming,archive.open(f'batch-{page:04d}/'+name,'w')as target:shutil.copyfileobj(incoming,target,64*1024)
            pages.append(dict(batch=page,tickets=len(folders[start:start+64]),records=result.get('records',result.get('record_count'))))
            part.unlink()
            if output.stat().st_size>MAX_OUTPUT:raise ValueError('Manual export exceeds512MiB; originals retained')
        if unavailable:
            from report_commitment import canonical_bytes
            best=io.StringIO(newline='');counts=io.StringIO(newline='');bw=csv.writer(best);cw=csv.writer(counts)
            bw.writerow(['ticket','lower_prime','upper_prime','gap','population']);cw.writerow(['ticket','centile','reported_count'])
            for identity in unavailable:
                file=tickets/identity/'network-report.json'
                if file.stat().st_size>262144:raise ValueError('Legacy report exceeds export budget')
                report=json.loads(file.read_bytes());body=report['body']
                if hashlib.sha256(canonical_bytes(body)).hexdigest()!=report['report_root']:raise ValueError('Retained legacy report commitment differs')
                computed=body['computed']
                if len(computed['best'])>100 or len(computed['counts'])!=100:raise ValueError('Legacy summary shape differs')
                for row in computed['best']:
                    lo=int(row['lower_prime']);gap=int(row['gap']);bw.writerow([identity,str(lo),str(lo+gap),str(gap),'retained_best_only'])
                for index,count in enumerate(computed['counts']):cw.writerow([identity,index,str(int(count))])
            archive.writestr('legacy-summary/retained-best.csv',best.getvalue());archive.writestr('legacy-summary/centile-counts.csv',counts.getvalue())
        archive.writestr('README.txt','PGN Horizon local search results\nEach batch contains observations.csv with exact integer endpoints and gap sizes, gap-sizes.csv, merit-bands.csv and provenance. The collection cutoff is recorded in each manifest. Legacy-summary retains the available best results and centile counts of older tickets whose full population was not conserved; those rows are explicitly partial. Missing observations are never reconstructed. These are local search observations, not a claim that every row has independent network acceptance. Keep unpublished local results private until their audits are committed. No wallet keys, cookies, or assigned private audit partitions are exported.\n')
        archive.writestr('manifest.json',json.dumps(dict(schema='PGN_MANUAL_SEARCH_RESULTS_V1',genesis=genesis,tickets=len(folders),batches=pages,unavailable_ticket_ids=unavailable,private_keys_included=False,network_acceptance_proven=False),indent=2))
    return dict(PASS=True,tickets=len(folders),batches=len(pages),bytes=output.stat().st_size)

def main(argv=None):
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--state-root',required=True,type=Path);parser.add_argument('--genesis',required=True)
    args=parser.parse_args(argv)
    with tempfile.TemporaryDirectory(prefix='pgn-result-download-')as tmp:
        output=Path(tmp)/'results.zip';export(args.state_root,args.genesis,output)
        with output.open('rb')as source:shutil.copyfileobj(source,sys.stdout.buffer,64*1024)
        sys.stdout.buffer.flush()
    return 0
