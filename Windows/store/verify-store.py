"""Read-only verification of the generated Store archive and its release assets."""
import hashlib
import io
import json
from pathlib import Path
import struct
import sys
import xml.etree.ElementTree as ET
import zipfile

KIT = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[2] / 'Store' / 'Microsoft-Store-2026-10-20'
PROJECT = KIT.parents[1]
identity = json.loads((KIT / 'identity.json').read_text(encoding='utf-8'))
upload = KIT / 'packages' / 'Sukut_0.2.34.0_x64.msixupload'
package = KIT / 'packages' / 'Sukut-0.2.34.0-x64-Store.msix'
F = '{http://schemas.microsoft.com/appx/manifest/foundation/windows10}'
R = '{http://schemas.microsoft.com/appx/manifest/foundation/windows10/restrictedcapabilities}'
model_hash = 'dcbf26c6d59847969bd1d1d387d9f0f79838053324d4837891c5be19aadd288e'
report = {'status': 'candidate-archive-verified', 'installed_msix_tested': False, 'audio_tested': False,
          'microsoft_certified': False, 'redistribution_permission': 'pending', 'driver_exception': 'pending'}
with zipfile.ZipFile(package) as z:
    assert z.testzip() is None, 'Archive CRC failure'
    names = z.namelist()
    assert all(not p.startswith(('/', '\\')) and '..' not in p.replace('\\', '/').split('/') for p in names)
    manifest = ET.fromstring(z.read('AppxManifest.xml'))
    ident = manifest.find(F + 'Identity')
    assert ident.attrib == {'Name': identity['packageName'], 'Publisher': identity['publisher'],
                            'Version': identity['packageVersion'], 'ProcessorArchitecture': 'x64'}
    assert manifest.find(F+'Properties/'+F+'PublisherDisplayName').text == identity['publisherDisplayName']
    assert manifest.find(F+'Applications/'+F+'Application').get('Executable') == 'Sukut.exe'
    assert any(c.get('Name') == 'runFullTrust' for c in manifest.findall(F+'Capabilities/'+R+'Capability'))
    assert 'resources.pri' in names and 'SukutVolumeGuard.exe' in names
    assert 'AppxSignature.p7x' not in names, 'Unexpected signing identity; this is an unsigned Store-ingestion candidate'
    forbidden = [p for p in names if Path(p).suffix.lower() in ('.sys','.cat','.inf','.pfx','.p12','.jks','.keystore','.pdb')
                 or any(x in p.lower() for x in ('/runtime/', 'signing.properties', 'password', 'vbcable_setup', 'recorder-compat'))]
    assert not forbidden, f'Private, driver, debug or live-runtime content: {forbidden}'
    assert not any(Path(p).name.lower() in ('onnxruntime.dll','directml.dll') for p in names)
    model = z.read('Assets/engine/separator.tflite')
    assert hashlib.sha256(model).hexdigest() == model_hash
    for window in ('window_576_q15.bin','window_960_q15.bin'):
        original = PROJECT / 'Windows' / 'assets' / 'engine' / window
        assert z.read('Assets/engine/'+window) == original.read_bytes()
    apk = z.read('Assets/Phone/Sukut.apk')
    apk_path = PROJECT / 'Releases' / '0.2.33-0.3.31' / 'Sukut-0.3.31-Android.apk'
    assert hashlib.sha256(apk).digest() == hashlib.sha256(apk_path.read_bytes()).digest()
    assert z.read('Assets/Phone/Sukut.apk.version').decode('ascii').split() == ['0.3.31','37']
    assert hashlib.sha256(apk).hexdigest().lower() == z.read('Assets/Phone/Sukut.apk.sha256').decode('ascii').strip().lower()
    assert any(p.startswith('licenses/') for p in names) and 'THIRD_PARTY_NOTICES.txt' in names
    report.update(package_name=ident.get('Name'), publisher=ident.get('Publisher'), version=ident.get('Version'),
                  archive_entries=len(names), installed_payload_bytes=sum(i.file_size for i in z.infolist()),
                  msix_bytes=package.stat().st_size, model_bytes=len(model), model_sha256=model_hash,
                  bundled_apk_bytes=len(apk), bundled_apk_sha256=hashlib.sha256(apk).hexdigest(),
                  original_model_and_windows_unchanged=True, separate_driver_not_bundled=True,
                  no_signing_keys_or_live_runtime=True, unused_windows_ml_files_excluded=True)
with zipfile.ZipFile(upload) as z:
    assert z.testzip() is None
    packages = [p for p in z.namelist() if p.endswith('.msix')]
    assert len(packages) == 1
    assert hashlib.sha256(z.read(packages[0])).digest() == hashlib.sha256(package.read_bytes()).digest()
report['msixupload_bytes'] = upload.stat().st_size
for locale in ('en-US', 'ar-SA'):
    folder = KIT / 'listing' / locale
    assert len((folder/'short-description.txt').read_text(encoding='utf-8')) <= 1000
    assert len((folder/'description.txt').read_text(encoding='utf-8')) <= 10000
    assert len([line for line in (folder/'keywords.txt').read_text(encoding='utf-8').splitlines() if line.strip()]) == 7
for name, expected in [('AppTile-300.png',(300,300)),('AppIcon-512.png',(512,512))]:
    header=(KIT/'assets'/name).read_bytes()[:24]
    assert header[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II',header[16:24]) == expected
report['listing_and_logo_structure_verified'] = True
(KIT/'evidence'/'package-verification.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
