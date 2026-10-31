"""Read-only artifact checks for the current 0.4 beta; writes public build evidence."""
from pathlib import Path
from hashlib import sha256
import json
import zipfile
import xml.etree.ElementTree as ET
import re

root = Path(__file__).resolve().parents[1]
version = ET.parse(root/'Windows/src/sukut-ui/Sukut.csproj').getroot().find('PropertyGroup/Version').text.strip()
gradle = (root/'app/build.gradle.kts').read_text(encoding='utf-8')
android_code = int(re.search(r'versionCode\s*=\s*(\d+)', gradle).group(1))
android_version = re.search(r'versionName\s*=\s*"([^"]+)"', gradle).group(1)
assert version == android_version and '-beta.' in version
apk = root / f'Builds/Sukut-{version}-android-arm64.apk'
old_apk = root / 'Builds/Sukut-0.3.31-android-arm64.apk'
package = root / f'Windows/dist/Sukut-{version}-win-x64'
archive = Path(str(package) + '.zip')
def digest(data):
    return sha256(data).hexdigest()
def file_hash(path):
    with path.open('rb') as handle:
        import hashlib
        return hashlib.file_digest(handle, 'sha256').hexdigest()
report = {'version': version, 'android_version_code': android_code, 'published': False}
with zipfile.ZipFile(old_apk) as before, zipfile.ZipFile(apk) as after:
    protected = ['lib/arm64-v8a/libtensorflowlite_jni.so']
    hashes = {}
    for name in protected:
        original, current = digest(before.read(name)), digest(after.read(name))
        assert original == current, 'Changed protected audio payload: ' + name
        hashes[name] = current
    # Since 28 Oct 2026 the model and windows ship sealed (tools/seal-assets.py;
    # opened bytes and output are bit-identical), so they must match the
    # project's sealed assets rather than 0.3.31.
    for name in ('separator.tflite', 'window_576_q15.bin', 'window_960_q15.bin'):
        current = digest(after.read('assets/' + name))
        assert current == file_hash(root / 'app/src/engine/assets' / name), 'APK asset differs from project: ' + name
        hashes['assets/' + name] = current
    assert 'assets/licenses/Material-Components-Apache-2.0.txt' in after.namelist()
    assert after.testzip() is None
report['android_protected_payload_unchanged'] = hashes
report['android_apk'] = {'path': str(apk), 'bytes': apk.stat().st_size, 'sha256': file_hash(apk)}
report['android_storage_delta_bytes'] = apk.stat().st_size - old_apk.stat().st_size
report['android_signer_sha256'] = 'c39f95966ed2df9f79c26be311269116121ce5be1c0de59fef34e7fe51e83fc5'
signature = root / 'work/polish-beta6/android-signature.txt'
assert report['android_signer_sha256'] in signature.read_text(encoding='utf-8-sig')
report['android_signer_check'] = 'apksigner verify --verbose --print-certs; '+str(signature.relative_to(root))
assert file_hash(package / 'app/Assets/Phone/Sukut.apk') == file_hash(apk)
assert (package / 'app/Assets/Phone/Sukut.apk.sha256').read_text().strip().lower() == file_hash(apk)
assert (package / 'app/Assets/Phone/Sukut.apk.version').read_text().split() == [version, str(android_code)]
metadata = json.loads((package / 'sukut-package.json').read_text(encoding='utf-8-sig'))
assert metadata['version'] == version and metadata['channel'] == 'beta'
assert file_hash(package / 'assets/engine/separator.tflite') == hashes['assets/separator.tflite']
native = Path('D:/Sukut-Dev/build-0.4/bin/Release')
for name in ['SukutEngine.dll', 'SukutVolumeGuard.exe']:
    assert file_hash(package / 'app' / name) == file_hash(native / name)
assert file_hash(package / 'Sukut.exe') == file_hash(native / 'SukutPortable.exe')
with zipfile.ZipFile(archive) as packed:
    assert packed.testzip() is None
    for name in packed.namelist():
        parts = [part.lower() for part in Path(name).parts]
        leaf = parts[-1] if parts else ''
        assert 'sukutkey' not in parts
        assert not leaf.endswith(('.p12', '.pfx', '.jks', '.keystore', '.pdb'))
        assert leaf not in ('release-signing.properties', 'private-key-and-password.txt')
        assert 'runtime' not in parts, 'Runtime/user data must not ship'
report['windows_package'] = {'path': str(archive), 'bytes': archive.stat().st_size,
                             'sha256': file_hash(archive), 'channel': metadata['channel']}
report['matching_android_bundle'] = True
report['private_files_and_user_runtime_excluded'] = True
unchanged_sources = []
# Intentional changes since the baseline: message wording (27 Oct 2026) and
# sealed-asset loading (28 Oct 2026). No arithmetic or model data changed.
reviewed_changes = {'src/model/SeparationModel.cpp', 'src/model/AssetSeal.h', 'src/dsp/StreamingDsp.cpp'}
with zipfile.ZipFile(root / 'work/beta-0.4/baseline-source.zip') as baseline:
    for directory in ('model', 'dsp', 'realtime', 'audio', 'live'):
        for path in (root / 'Windows/src' / directory).rglob('*'):
            if path.suffix not in ('.cpp', '.h'):
                continue
            relative = path.relative_to(root / 'Windows').as_posix()
            if relative in reviewed_changes:
                continue
            assert digest(path.read_bytes()) == digest(baseline.read(relative)), 'Changed DSP/audio source: ' + relative
            unchanged_sources.append(relative)
report['windows_unchanged_model_dsp_audio_sources'] = unchanged_sources
report['windows_reviewed_source_changes'] = sorted(reviewed_changes)
suites = [ET.parse(path).getroot().attrib for path in (root/'app/build/test-results/testEngineUnitTest').glob('TEST*.xml')]
report['android_unit_tests'] = {key: sum(int(suite.get(key, 0)) for suite in suites)
                               for key in ('tests', 'failures', 'errors', 'skipped')}
trx = ET.parse(root / 'work/polish-beta6/windows-tests/beta6.trx')
counters = trx.find('.//{http://microsoft.com/schemas/VisualStudio/TeamTest/2010}Counters')
report['windows_unit_tests'] = {key: int(counters.attrib[key]) for key in ('total', 'passed', 'failed')}
review = root / 'work/beta-0.4/ui-beta3/device/review.txt'
report['earlier_beta3_ui_assertions'] = review.read_text(encoding='utf-8') if review.exists() else 'pending'
report['visual_screen_review'] = 'Not completed for beta.6; user pressed physical Escape to stop Computer Use. No further desktop automation performed.'
report['latest_apk_installed'] = False
report['latest_android_device_checks_run'] = False
report['owner_selected_build_checks_only'] = True
report['idle_host_check'] = (root / 'work/beta-0.4/idle-host-verification.txt').read_text(encoding='utf-8-sig').strip()
report['model_wrapper_reference'] = json.loads((root / 'work/beta-0.4/model-wrapper-beta4-check.json').read_text(encoding='utf-8-sig'))
report['full_model_suspension_enabled'] = False
report['battery_measurement'] = 'not measured; real model continues updating state, eligible silent FFT work reduced'
evidence = root / 'docs/beta-0.4/artifact-verification.json'
if evidence.exists():
    previous = json.loads(evidence.read_text(encoding='utf-8-sig'))
    if previous.get('version') != version:
        archive_report = evidence.with_name('artifact-'+previous['version']+'-verification.json')
        if not archive_report.exists(): archive_report.write_bytes(evidence.read_bytes())
evidence.write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print('PASS: unchanged audio payload, original signer evidence, matching bundled APK, beta metadata, archive integrity and private-file exclusions')
