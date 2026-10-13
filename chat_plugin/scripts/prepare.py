#!/usr/bin/env python3
"""Fetch pinned developer dependencies. No models or account credentials."""
import hashlib
import json
from pathlib import Path
import subprocess
import tarfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'chat_plugin'
DEPS = ROOT / '.phase0/chat/deps'
ARCHIVES = ROOT / '.phase0/chat/archives'


def main():
    lock = json.loads((PROJECT / 'dependencies.lock.json').read_text())
    ARCHIVES.mkdir(parents=True, exist_ok=True)
    DEPS.mkdir(parents=True, exist_ok=True)
    for name, asset in lock.items():
        archive = ARCHIVES / name
        if not archive.exists() or hashlib.sha256(archive.read_bytes()).hexdigest() != asset['sha256']:
            temporary = archive.with_suffix('.partial')
            subprocess.run(['curl', '-sS', '--fail', '--location', '--proto', '=https', '--proto-redir', '=https',
                            asset['url'], '-o', str(temporary)], check=True)
            if temporary.stat().st_size != asset['size'] or hashlib.sha256(temporary.read_bytes()).hexdigest() != asset['sha256']:
                raise RuntimeError(f'Dependency hash mismatch: {name}')
            temporary.replace(archive)
        if name == 'ctranslate2_macos_wheel':
            output = DEPS / 'lib/libawl-ctranslate2.dylib'
            output.parent.mkdir(exist_ok=True)
            with zipfile.ZipFile(archive) as z:
                member = 'ctranslate2/.dylibs/libctranslate2.4.8.2.dylib'
                output.write_bytes(z.read(member))
            subprocess.run(['install_name_tool', '-id', '@rpath/libawl-ctranslate2.dylib', str(output)], check=True)
            subprocess.run(['codesign', '--force', '--sign', '-', str(output)], check=True)
            continue
        destination = DEPS / ('ct2' if name == 'ctranslate2' else name)
        if destination.exists() and any(destination.iterdir()):
            continue
        destination.mkdir(parents=True, exist_ok=True)
        with tarfile.open(archive) as tar:
            # Official checksum-pinned archives; still use Python's safe filter.
            members = tar.getmembers()
            if name != 'qt':
                for member in members:
                    member.name = '/'.join(Path(member.name).parts[1:])
                members = [member for member in members if member.name]
            tar.extractall(destination, members=members, filter='data')
    print('Pinned developer dependencies ready.')


if __name__ == '__main__':
    main()
