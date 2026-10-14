"""Wrap a checked package for download; hash the exact finished files."""
import json
from pathlib import Path
import shutil
import sys
import zipfile
from package_source import ROOT, sha256, source_info


def main():
    platform = sys.argv[1]
    if platform not in ('macos-arm64', 'windows-x64'):
        raise ValueError('Expected macos-arm64 or windows-x64')
    package = ROOT / '.phase0/packages' / ('obs-chat-mac' if platform == 'macos-arm64' else 'obs-chat-win')
    evidence = json.loads((package / 'package.json').read_text())
    if evidence['source'] != source_info() or evidence['source']['dirty']:
        raise RuntimeError('Build and distribute from the same clean Git checkout')
    version = evidence['version']
    name = f'Interpretwitch-{version}-{platform}'
    output = ROOT / '.phase0/release' / platform
    if output.exists():
        raise RuntimeError(f'Refusing to overwrite release files: {output}')
    output.mkdir(parents=True)
    bundle = package / ('awl-chat.plugin' if platform == 'macos-arm64' else 'awl-chat')
    layout = ('Contents/MacOS/awl-chat', 'Contents/MacOS/awl-chat-worker', 'Contents/Resources/source.zip') if platform == 'macos-arm64' else ('bin/64bit/awl-chat.dll', 'bin/64bit/awl-chat-worker.exe', 'data/source.zip')
    for key, relative in zip(('module_sha256', 'helper_sha256', 'source_sha256'), layout):
        if sha256(bundle / relative) != evidence[key]:
            raise RuntimeError(f'Packaged file changed since validation: {relative}')
    with zipfile.ZipFile(output / (name + '.zip'), 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(bundle.rglob('*')):
            if path.is_symlink():
                raise RuntimeError('Release bundle contains a symlink')
            if path.is_file():
                archive.write(path, path.relative_to(package).as_posix())
        archive.write(ROOT / 'DOWNLOADS.md', 'READ-ME.md')
        archive.write(package / 'package.json', 'package.json')
        if platform == 'macos-arm64':
            archive.write(ROOT / 'chat_plugin/scripts/install-mac.sh', 'Install Interpretwitch.command')
    if platform == 'windows-x64':
        installer = package / f'Interpretwitch-{version}-windows-x64-setup.exe'
        shutil.copy2(installer, output / installer.name)
    evidence['artifacts'] = {p.name: {'sha256': sha256(p), 'bytes': p.stat().st_size}
                             for p in sorted(output.iterdir())}
    (output / (name + '-build.json')).write_text(json.dumps(evidence, indent=2) + '\n', encoding='utf-8')
    checksums = ''.join(f'{sha256(p)}  {p.name}\n' for p in sorted(output.iterdir()))
    (output / ('SHA256SUMS-' + platform + '.txt')).write_text(checksums, encoding='utf-8')
    print(output)


if __name__ == '__main__':
    main()
