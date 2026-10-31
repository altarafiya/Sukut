"""Restore pinned source archives only; never downloads or modifies model weights."""
import hashlib
import pathlib
import urllib.request
import zipfile
import sys

root = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else 'D:/Sukut-Dev/deps')
archives = [
    ('tensorflow-2.16.1', 'https://github.com/tensorflow/tensorflow/archive/refs/tags/v2.16.1.zip',
     '54c976f828182f85e10e03840dd3b0504109f57760a498075574e35e9aa983fe'),
    ('kissfft-131.1.0', 'https://github.com/mborgerding/kissfft/archive/refs/tags/131.1.0.zip',
     '9c2e19cc34ed910dcb509fd8ab561a523b923b6578703ace8c8f37f5a286bb25'),
    ('FXdiv-63058eff77e11aa15bf531df5dd34395ec3017c8',
     'https://codeload.github.com/Maratyszcza/FXdiv/zip/63058eff77e11aa15bf531df5dd34395ec3017c8',
     '3d7b0e9c4c658a84376a1086126be02f9b7f753caa95e009d9ac38d11da444db'),
]
for name, url, expected in archives:
    archive = root / 'downloads' / (name + '.zip')
    destination = root / 'src' / name
    archive.parent.mkdir(parents=True, exist_ok=True)
    if not archive.exists():
        temporary = archive.with_suffix('.download')
        with urllib.request.urlopen(url, timeout=180) as response, temporary.open('wb') as output:
            while chunk := response.read(1024 * 1024):
                output.write(chunk)
        temporary.replace(archive)
    actual = hashlib.file_digest(archive.open('rb'), 'sha256').hexdigest()
    if actual != expected:
        raise RuntimeError('Pinned dependency hash mismatch: ' + name)
    print(name + ': archive SHA-256 verified', flush=True)
    if (destination / '.sukut-verified-source').exists():
        continue
    with zipfile.ZipFile(archive) as source:
        for entry in source.infolist():
            relative = pathlib.PurePosixPath(entry.filename)
            if relative.is_absolute() or '..' in relative.parts or len(relative.parts) < 2:
                continue
            target = destination.joinpath(*relative.parts[1:])
            if entry.is_dir():
                target.mkdir(parents=True, exist_ok=True)
            else:
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.read(entry))
    (destination / '.sukut-verified-source').write_text(expected + '\n', encoding='ascii')
    print(name + ': source restored', flush=True)
