"""CI-only hourly activation for disconnected full-node qualification.

The repository's normal daemon keeps its disabled activation. This tool changes
one source file only in a disposable build and labels the resulting image.
"""
from pathlib import Path
import argparse,datetime,hashlib,json,os,shutil,subprocess
a=argparse.ArgumentParser();a.add_argument('--phase',choices=['prepare','collect'],required=True);args=a.parse_args()
root=Path.cwd();file=root/'wallet/riecoin-core-upstream-v2606/src/pgn/native_hourly_activation.cpp'
genesis='0ccd1f2349ed75f9cf4d0279871a204e107683e5baa1dd6641c4e6f08a25a17d';activation=int(datetime.datetime(2026,11,14,9,tzinfo=datetime.timezone.utc).timestamp())
original=b'#include <pgn/native_hourly_activation.h>\nnamespace pgn::core {\nstd::optional<NativeHourlyMeritActivation> ConfiguredNativeHourlyMerit(const uint256&) {\n    return std::nullopt;\n}\n}\n'
replacement=('#include <pgn/native_hourly_activation.h>\nnamespace pgn::core {\nstd::optional<NativeHourlyMeritActivation> ConfiguredNativeHourlyMerit(const uint256& chain) {\n    const uint256 expected{"'+genesis+'"};\n    if(chain!=expected)return std::nullopt;\n    return NativeHourlyMeritActivation{expected,'+str(activation)+'};\n}\n}\n').encode()
receipt=root/'build-native/hourly-fixture-intent.json'
if args.phase=='prepare':
 assert file.read_bytes().replace(b'\r\n',b'\n')==original,'Unexpected production activation source'
 assert os.environ.get('GITHUB_ACTIONS')=='true','This mutating phase is CI-only'
 assert not receipt.exists()
 value=dict(schema='PGN_PRIVATE_HOURLY_NODE_FIXTURE_V1',source=os.environ['GITHUB_SHA'],original_sha256=hashlib.sha256(original).hexdigest(),activation_sha256=hashlib.sha256(replacement).hexdigest(),genesis=genesis,median_time=activation,public_activation=False,scope='Isolated full-node cold/fork qualification only; never signed or published as a client update')
 receipt.write_text(json.dumps(value,indent=2)+'\n',encoding='utf8');file.write_bytes(replacement)
 print(json.dumps(dict(PASS=True,phase='prepare',genesis=genesis,activation=activation,public_activation=False)))
else:
 value=json.loads(receipt.read_bytes());assert file.read_bytes()==replacement
 output=root/'native-hourly-fixture';assert output.is_dir()
 image=output/('pgn-core.exe'if os.name=='nt'else'pgn-core');assert image.is_file()
 value.update(image_sha256=hashlib.sha256(image.read_bytes()).hexdigest(),image_bytes=image.stat().st_size,files=[dict(path=f.name,bytes=f.stat().st_size,sha256=hashlib.sha256(f.read_bytes()).hexdigest())for f in output.iterdir()if f.is_file()])
 (output/'PRIVATE-HOURLY-FIXTURE.json').write_text(json.dumps(value,indent=2)+'\n',encoding='utf8')
 print(json.dumps(dict(PASS=True,phase='collect',image_sha256=value['image_sha256'],private_only=True)))
