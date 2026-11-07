"""Offline public ZIP configuration check; never extracts or executes its contents.

This checks archive layout and bounded text configuration, not arbitrary binary
semantics or network/scientific readiness. Public seed descriptors are expected;
operator workspaces, wallets, credentials and machine profiles are forbidden.
"""
from pathlib import Path,PurePosixPath
import argparse,hashlib,json,re,zipfile

MAX_ARCHIVE=2*1024**3
MAX_ENTRIES=4096
MAX_TEXT=1024**2
PRIVATE_PARTS={'.ssh','wallets','chainstate','blocks','backups','logs','ui-runtime',
               'cpu-normal-cycle','local-engine','interactive-mining'}
PRIVATE_FILES={'wallet.dat','.cookie','wallet-core.json','wallet-owned.json','worker-stream.json',
               'cpu-controllers.json','cpu-inventory.json','controller.json','shared68-checkpoint.json'}
TEXT={'.json','.txt','.ini','.conf','.bat','.cmd','.ps1','.sh','.py','.pem'}
HOME=re.compile(r'(?i)(?:[a-z]:[\\/]+Users[\\/]+[^\\/\s"\']+|/(?:home|Users)/[^/\s"\']+)')
KEY=re.compile(r'-----BEGIN (?:RSA |EC |OPENSSH |ENCRYPTED )?PRIVATE KEY-----')

def inspect(path):
 path=Path(path)
 if not path.is_file()or not 0<path.stat().st_size<=MAX_ARCHIVE:raise ValueError('Bounded public ZIP required')
 rejected=[];text_files=0;seen=set();total=0
 with zipfile.ZipFile(path)as archive:
  entries=archive.infolist()
  if not 1<=len(entries)<=MAX_ENTRIES:raise ValueError('Public archive entry budget exceeded')
  for item in entries:
   name=item.filename;parts=PurePosixPath(name).parts;lower=[p.lower()for p in parts]
   reason=None
   if '\\'in name or name.startswith('/')or re.match(r'^[A-Za-z]:',name)or '..'in parts or not parts:reason='unsafe_archive_path'
   elif name.casefold()in seen:reason='duplicate_archive_path'
   elif any(p in PRIVATE_PARTS for p in lower)or lower[-1]in PRIVATE_FILES:reason='operator_state_or_profile'
   elif lower[-1].endswith(('.key','.pfx','.p12'))or lower[-1].startswith(('id_ed25519','id_rsa')):reason='private_key_file'
   seen.add(name.casefold());total+=item.file_size
   if total>MAX_ARCHIVE:raise ValueError('Expanded public archive budget exceeded')
   if item.file_size and item.compress_size and item.file_size>512*item.compress_size:reason='excessive_compression_ratio'
   if not item.is_dir()and Path(name).suffix.lower()in TEXT:
    if item.file_size>MAX_TEXT:reason='configuration_exceeds_text_budget'
    else:
     text_files+=1;text=archive.read(item).decode('utf-8-sig',errors='replace')
     if KEY.search(text):reason='private_key_material'
     elif HOME.search(text):reason='absolute_personal_home_path'
     elif re.search(r'(?im)^\s*(?:rpcpassword|rpcauth)\s*=\s*\S',text):reason='embedded_RPC_credential'
   if reason:
    rejected.append(dict(path=name,reason=reason))
    if len(rejected)>=64:break
 with path.open('rb')as stream:sha=hashlib.file_digest(stream,'sha256').hexdigest()
 return dict(schema='PGN_PUBLIC_PACKAGE_CONFIGURATION_CHECK_V1',PASS=not rejected,
  archive_sha256=sha,entries=len(entries),expanded_bytes=total,text_files_checked=text_files,
  rejected=rejected,scope='Archive layout and bounded text configuration only; no execution, binary semantic audit or network/scientific admission')

def main():
 parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('archive',type=Path);args=parser.parse_args()
 result=inspect(args.archive);print(json.dumps(result,separators=(',',':')));return 0 if result['PASS']else 1
if __name__=='__main__':raise SystemExit(main())
