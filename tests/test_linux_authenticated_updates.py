"""Reference-TUF authentication at the Linux catalogue/staging boundary."""
from pathlib import Path
import json,sys,unittest
P=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(P/'tools'),str(P/'src/update')]
import test_authenticated_updates as signed
import stage_component_bundle as stage
from tuf.api.exceptions import LengthOrHashMismatchError,DownloadLengthMismatchError


class LinuxCatalogTests(unittest.TestCase):
    def setUp(self):
        self.f=signed.UpdateTests();self.f.setUp();f=self.f
        f.context['platform']='linux-x86_64'
        f.context,f.context_sha=stage.context_from_bytes(json.dumps(f.context).encode())
        image=bytearray(64);image[:6]=b'\x7fELF\x02\x01';image[18:20]=b'\x3e\x00';f.pe=bytes(image)
        f.manifest['platform']='linux-x86_64';row=f.manifest['components'][0];row['primary']='payload/cpu/controller'
        row['files']=[dict(path=row['primary'],bytes=len(f.pe),sha256=stage.sha(f.pe))];f.publish()

    def tearDown(self):self.f.tearDown()

    def test_signed_linux_catalogue_stages_exact_elf_without_using_windows_catalogue(self):
        f=self.f;self.assertNotIn(f.target+'components.json',f.transport.files)
        view=f.client().refresh(['cpu']);self.assertTrue(view['publisher_authenticated'])
        result=f.client().prepare(['cpu'])
        self.assertEqual((Path(result['stage'])/'payload/cpu/controller').read_bytes(),f.pe)

    def test_signed_windows_manifest_cannot_be_installed_as_linux(self):
        f=self.f;f.manifest['platform']='windows-x86_64';f.publish()
        with self.assertRaisesRegex(stage.StageError,'platform'):f.client().prepare(['cpu'])

    def test_corrupt_linux_download_is_rejected_by_reference_tuf(self):
        f=self.f;f.transport.files[f.target+'components.zip']+=b'TAMPER'
        # The reference library may reject either at declared length or hash.
        with self.assertRaises((LengthOrHashMismatchError,DownloadLengthMismatchError)):f.client().prepare(['cpu'])
        self.assertFalse(any((f.root/'cache').glob('s-*')))


if __name__=='__main__':unittest.main()
