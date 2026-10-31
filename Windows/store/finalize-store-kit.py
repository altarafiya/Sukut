"""Assemble only allowlisted Store deliverables; never zip the scratch tree."""
import hashlib
import json
from pathlib import Path
import sys
import zipfile

kit = Path(sys.argv[1]).resolve()
project = kit.parents[1]
store_tools = project / 'Windows' / 'store'
evidence = kit / 'evidence'
report = json.loads((evidence / 'package-verification.json').read_text(encoding='utf-8'))
assert report['status'] == 'candidate-archive-verified'
assert report['version'] == '0.2.34.0'
assert not report['installed_msix_tested'] and not report['microsoft_certified']

readme = """# Sukut — Microsoft Store preparation kit

Prepared 20 October 2026 for **Altarafiyah.Sukut**, Store ID **9NL67C3CS7QR**.

## Start here

1. Read documents/READINESS.md. This is a candidate, not Microsoft approval.
2. Follow documents/SUBMISSION-STEPS.md to prepare the Partner Center draft.
3. For the Packages field, use **packages/Sukut_0.2.34.0_x64.msixupload**.
   Do not upload this whole ZIP or the desktop EXE installer.
4. Keep the submission in draft until rights, dependency/API review, hosted
   privacy policy and installed-MSIX testing are resolved. Select and verify the
   manual publishing hold before any eventual certification submission.

## Contents

- listing/en-US and listing/ar-SA: descriptions, features, keywords and release notes.
- artwork/en and artwork/ar: five 1920 × 1080 Store panels per language, plus editable HTML.
- artwork/contact-sheet.png: overview of all ten panels.
- assets: approved Sukut logo at the required image sizes.
- screenshots: actual Windows captures used by the artwork.
- documents: privacy drafts, reviewer notes, field values, readiness, provenance and sources.
- identity.json: owner-provided exact Store identity.
- packages: the single Store upload container, with model and Android companion included.
- evidence: concise validation reports only in the portable ZIP.
- source-tools: reproducible preparation, verification and artwork scripts in the ZIP.
- manifest.json and SHA256SUMS.txt: inventory and hashes of the included files.

Windows candidate **0.2.34.0**, x64, self-contained WinUI 3/.NET/native engine.
Bundled signed Android companion **0.3.31/code37**. The existing 0.2.33 desktop
installation and release were not replaced. Preparing this kit changed no driver
or Windows security settings and submitted nothing to the Store.

MSIX excludes the separate VB-CABLE driver and installer. The dependency still
requires Microsoft's review; exclusion alone is not Store acceptance. Publicly
host the reviewed privacy policy before entering its URL in Partner Center.

The unsigned MSIXUPLOAD is intended for Store ingestion and signing by Microsoft,
not direct double-click installation. No signing key, password, live runtime
data, scratch payload or original-source backup is included in this portable ZIP.
"""
(kit / 'README.md').write_text(readme, encoding='utf-8')

build_report = """# Build and validation — 20 October 2026

## Result

Prepared **Sukut 0.2.34.0 x64** for the exact reserved Microsoft Store identity.
This is an unsigned Store-ingestion candidate, not a certified or publicly
released build. The Store upload container includes one canonical MSIX.

- MSIX: 84,260,630 bytes (84.26 MB decimal).
- MSIXUPLOAD: 84,260,774 bytes.
- Installed archive payload: 210,834,379 bytes across 596 entries.
- Included signed Android companion: 0.3.31/code37, 7,512,103 bytes.
- Model and DSP window assets verified byte-for-byte unchanged.
- Two unused native Windows ML files excluded: 40,359,504 uncompressed bytes.
  No measured RAM or audio-latency improvement is claimed for this packaging change.

## Implementation

- Store identity is injected from identity.json into the manifest template.
- Added an explicit resource/data split: packaged resources remain read-only;
  packaged preferences, routing journals and crash data use app-local writable storage.
- Added Sukut_CreateWithPaths while retaining the existing Sukut_Create ABI and
  unpackaged path behavior. Model/DSP gains and processing parameters were not changed.
- Self-contained x64 MSIX has runFullTrust and internetClient capabilities, no
  driver, driver installer, NT service, signing secrets or live routing state.
- AVX support is checked before opening the packaged application's main window.
- Store payload exclusion also removes unused native Windows ML DLLs from MSIX,
  rather than applying only to unpackaged publish output.
- English/Arabic listing drafts and ten artwork panels use real Windows UI captures.

## Completed checks

- Native and WinUI Release compilation succeeded.
- Final Store publish/package build succeeded after correcting the Store resource
  index target and excluding the unused native payload.
- ZIP CRC, safe archive paths, manifest identity/version/capabilities, notices,
  dependency assets, model hash, DSP window bytes and bundled APK hash verified.
- Canonical MSIX extracted from MSIXUPLOAD matches its embedded bytes. SDK Test
  MSIX has the same entry contents but different container bytes; it is not the upload.
- Native controller initialized with separate resource/data roots and destroyed
  successfully. This probe started no audio pipeline and changed no audio route.
- Listing field lengths/keyword structure and logo dimensions checked.
- English/Arabic Windows UI captured with processing OFF and original controls
  retained; language was restored to English. These are installed 0.2.33 UI captures,
  not evidence of deployed 0.2.34 MSIX testing.

## Not performed / not established

- No MSIX deployment, installed-package UI/audio validation or Windows App
  Certification Kit pass is claimed.
- No audio quality, latency or stability measurement was run for this packaging update.
- No trusted third-party Windows certificate was generated or imported.
- No existing app or driver was uninstalled, installed or replaced.
- No GitHub push, public privacy hosting, Partner Center upload, certification
  submission or publication was performed.
- Microsoft's driver/internal-routing-API acceptance remains pending.

See READINESS.md before proceeding. Local build logs and original-source snapshots
remain in the working folder and are intentionally excluded from the portable ZIP.
"""
(kit / 'documents' / 'BUILD-REPORT.md').write_text(build_report, encoding='utf-8')

sources = """# Official references

Reviewed for this preparation on 20 October 2026. Partner Center labels may change.
The portal announces policy changes effective 22 October 2026; review the policy
version effective on the actual submission date.

- [Microsoft Store policies](https://learn.microsoft.com/en-us/windows/apps/publish/store-policies):
  10.2.4 dependency/driver review; 10.2.8 Windows settings; privacy requirements.
- [Get started publishing](https://learn.microsoft.com/en-us/windows/apps/publish/get-started):
  MSIX Store signing and publishing routes.
- [MSI/EXE package requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements):
  why the current unsigned desktop EXE is not the Store submission package.
- [MSIX submission options](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msix/manage-submission-options):
  select Don't publish this submission until I select Publish now.
- [Screenshots and images](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/screenshots-and-images):
  listing artwork dimensions and screenshot guidance.
- [Self-contained Windows App SDK deployment](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/self-contained-deploy/deploy-self-contained-apps).
- [Single-project MSIX](https://learn.microsoft.com/en-us/windows/apps/windows-app-sdk/single-project-msix).
- [Prepare desktop applications for packaging](https://learn.microsoft.com/en-us/windows/msix/desktop/desktop-to-uwp-prepare):
  packaged application constraints; a Windows driver is not an MSIX payload.

These references explain requirements; they are not approval of Sukut or its dependencies.
The driver-review request included in this kit has not been sent.
"""
(kit / 'documents' / 'SOURCES.md').write_text(sources, encoding='utf-8')

for name in ('STORE-FIELDS.md', 'SUBMISSION-STEPS.md'):
    p = kit / 'documents' / name
    s = p.read_text(encoding='utf-8')
    s = s.replace('Screenshots: actual Windows app captures only; captions supplied separately. No phone screenshots as evidence of a Windows feature.',
                  'Screenshots: use artwork/en or artwork/ar, five 1920 × 1080 panels per locale combining actual Windows captures with typography/illustrations. See artwork/inventory.json and ARTWORK.md; the smaller raw captures are not upload assets. No phone screenshots as evidence of a Windows feature.')
    s = s.replace('Upload the corresponding Store logo and Windows screenshots. Use the image/caption inventory;',
                  'Upload assets/AppTile-300.png and the five corresponding artwork/<language> PNGs. Use artwork/inventory.json for captions;')
    p.write_text(s, encoding='utf-8')

files = {}
def include(source, arc):
    source = source.resolve()
    assert source.is_relative_to(project), source
    assert source.is_file(), source
    assert arc not in files and not arc.startswith('/') and '..' not in Path(arc).parts
    files[arc] = source

for name in ('README.md', 'identity.json'):
    include(kit / name, name)
for directory in ('listing', 'documents', 'assets', 'screenshots', 'artwork'):
    for p in sorted((kit / directory).rglob('*')):
        if p.is_file():
            assert p.suffix.lower() in ('.md', '.txt', '.json', '.png', '.html', '.appxmanifest'), p
            include(p, p.relative_to(kit).as_posix())
include(kit / 'packages' / 'Sukut_0.2.34.0_x64.msixupload', 'packages/Sukut_0.2.34.0_x64.msixupload')
for name in ('package-verification.json', 'native-path-probe.json'):
    include(evidence / name, 'evidence/' + name)
for p in sorted(store_tools.iterdir()):
    if p.is_file() and p.suffix.lower() in ('.py', '.ps1', '.template', '.cjs'):
        include(p, 'source-tools/' + p.name)

records = [{'path': name, 'bytes': p.stat().st_size, 'sha256': hashlib.sha256(p.read_bytes()).hexdigest()}
           for name, p in sorted(files.items())]
manifest = {'prepared_date': '2026-10-20', 'version': '0.2.34.0', 'store_id': '9NL67C3CS7QR',
            'status': 'local-submission-candidate', 'published': False, 'installed_msix_tested': False,
            'excluded': ['scratch payloads', 'live runtime data', 'original source backup', 'private signing keys',
                         'duplicate MSIX and SDK test-install folder'], 'files': records}
(kit / 'manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
manifest_record = {'path': 'manifest.json', 'sha256': hashlib.sha256((kit / 'manifest.json').read_bytes()).hexdigest()}
checksums = ''.join(f"{r['sha256']}  {r['path']}\n" for r in records + [manifest_record])
(kit / 'SHA256SUMS.txt').write_text(checksums, encoding='utf-8')
include(kit / 'manifest.json', 'manifest.json')
include(kit / 'SHA256SUMS.txt', 'SHA256SUMS.txt')

archive = kit.parent / 'Sukut-0.2.34-Microsoft-Store-Kit.zip'
with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as z:
    for name, p in sorted(files.items()):
        z.write(p, name, compress_type=zipfile.ZIP_STORED if p.suffix in ('.png', '.msixupload') else zipfile.ZIP_DEFLATED)
with zipfile.ZipFile(archive) as z:
    assert z.testzip() is None, 'Portable kit CRC check failed'
    assert set(z.namelist()) == set(files), 'Portable kit inventory mismatch'
    for r in records + [manifest_record]:
        assert hashlib.sha256(z.read(r['path'])).hexdigest() == r['sha256'], r['path']
    assert len([n for n in z.namelist() if n.endswith('.msixupload')]) == 1
    assert not any('/runtime/' in n or '/capture-root/' in n or '/original-source/' in n for n in z.namelist())
digest = hashlib.sha256(archive.read_bytes()).hexdigest()
archive.with_suffix('.zip.sha256').write_text(f'{digest}  {archive.name}\n', encoding='utf-8')
print(json.dumps({'zip': str(archive), 'bytes': archive.stat().st_size, 'sha256': digest,
                  'files': len(files), 'artwork_pngs': len(list((kit / 'artwork' / 'en').glob('*.png'))) + len(list((kit / 'artwork' / 'ar').glob('*.png'))),
                  'crc_and_hashes_verified': True}, indent=2))
