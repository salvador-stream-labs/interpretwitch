#!/usr/bin/env python3
"""Fetch pinned Windows developer dependencies. No models or account credentials.

Run inside a Visual Studio x64 developer environment: import libraries for the
installed OBS and for the prebuilt CTranslate2 DLL are generated with dumpbin/lib.
"""
import json
import os
from pathlib import Path
import re
import subprocess
import tarfile
import zipfile
from package_source import sha256

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'chat_plugin'
DEPS = ROOT / '.phase0/chat/deps-win'
ARCHIVES = ROOT / '.phase0/chat/archives-win'
OBS_DIR = Path(os.environ.get('AWL_OBS_DIR', r'C:\Program Files\obs-studio'))
# Source archives shared with the Mac recipe. Mac binaries and SIMDe are not needed.
SHARED = ['qtwebsockets', 'ctranslate2', 'sentencepiece', 'fasttext']
CT2_RUNTIME = ['ctranslate2.dll', 'libiomp5md.dll']


def fetch(name, asset):
    archive = ARCHIVES / name
    if not archive.exists() or sha256(archive) != asset['sha256']:
        temporary = archive.with_suffix('.partial')
        subprocess.run(['curl.exe', '-sS', '--fail', '--location', '--proto', '=https', '--proto-redir', '=https',
                        asset['url'], '-o', str(temporary)], check=True)
        if temporary.stat().st_size != asset['size'] or sha256(temporary) != asset['sha256']:
            raise RuntimeError(f'Dependency hash mismatch: {name}')
        temporary.replace(archive)
    return archive


def extract_tar(archive, destination):
    if destination.exists() and any(destination.iterdir()):
        return
    destination.mkdir(parents=True, exist_ok=True)
    with tarfile.open(archive) as tar:
        members = []
        for member in tar.getmembers():
            member.name = '/'.join(Path(member.name).parts[1:])
            # Windows cannot create the few repository-script symlinks; none are needed.
            if member.name and not member.issym() and not member.islnk():
                members.append(member)
        tar.extractall(destination, members=members, filter='data')


def import_library(dll, output):
    """Create an MSVC import library from a DLL's export table."""
    exports = subprocess.check_output(['dumpbin', '/nologo', '/exports', str(dll)], text=True)
    names = re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]{8}\s+(\S+)', exports, re.MULTILINE)
    if not names:
        raise RuntimeError(f'No exports found in {dll}')
    definition = output.with_suffix('.def')
    definition.write_text(f'LIBRARY {dll.stem}\nEXPORTS\n' + ''.join(f'  {n}\n' for n in names))
    subprocess.run(['lib', '/nologo', '/machine:x64', f'/def:{definition}', f'/out:{output}'],
                   check=True, stdout=subprocess.DEVNULL)


def main():
    windows = json.loads((PROJECT / 'dependencies-windows.lock.json').read_text())
    shared = json.loads((PROJECT / 'dependencies.lock.json').read_text())
    ARCHIVES.mkdir(parents=True, exist_ok=True)
    (DEPS / 'bin').mkdir(parents=True, exist_ok=True)
    (DEPS / 'lib').mkdir(parents=True, exist_ok=True)

    for name in SHARED:
        extract_tar(fetch(name, shared[name]), DEPS / ('ct2' if name == 'ctranslate2' else name))
    extract_tar(fetch('obs_source', windows['obs_source']), DEPS / 'obs-studio')
    # Inflate support for the optional NVIDIA GPU pack archive (helper only).
    extract_tar(fetch('zlib', windows['zlib']), DEPS / 'zlib')

    qt = DEPS / 'qt'
    archive = fetch('qt', windows['qt'])
    if not (qt / 'lib/cmake/Qt6/Qt6Config.cmake').exists():
        with zipfile.ZipFile(archive) as z:
            z.extractall(qt)

    with zipfile.ZipFile(fetch('ctranslate2_windows_wheel', windows['ctranslate2_windows_wheel'])) as z:
        for name in CT2_RUNTIME:
            (DEPS / 'bin' / name).write_bytes(z.read('ctranslate2/' + name))
    import_library(DEPS / 'bin/ctranslate2.dll', DEPS / 'lib/ctranslate2.lib')

    obs_bin = OBS_DIR / 'bin/64bit'
    for name in ['obs', 'obs-frontend-api']:
        dll = obs_bin / f'{name}.dll'
        if not dll.is_file():
            raise RuntimeError(f'Install OBS 32.2.2 or set AWL_OBS_DIR. Missing {dll}')
        import_library(dll, DEPS / f'lib/{name}.lib')
    print('Pinned Windows developer dependencies ready.')


if __name__ == '__main__':
    main()
