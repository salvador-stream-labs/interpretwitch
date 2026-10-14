"""Provision the pinned OBS runtime on a disposable GitHub runner only."""
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import zipfile
from package_source import ROOT, sha256


def fetch(asset, destination):
    subprocess.run(['curl', '--fail', '--location', '--retry', '3', '--proto', '=https',
                    '--proto-redir', '=https', '--output', str(destination), asset['url']], check=True)
    if destination.stat().st_size != asset['size'] or sha256(destination) != asset['sha256']:
        raise RuntimeError('OBS download failed verification')


def main():
    if os.environ.get('GITHUB_ACTIONS') != 'true':
        raise RuntimeError('This script is only for disposable GitHub-hosted runners')
    platform = sys.argv[1]
    work = ROOT / '.phase0/ci'
    work.mkdir(parents=True, exist_ok=True)
    runtime = json.loads((ROOT / 'chat_plugin/ci-runtimes.lock.json').read_text())[platform]
    archive = work / ('obs.dmg' if platform == 'macos-arm64' else 'obs.zip')
    fetch(runtime, archive)
    variables = {}
    if platform == 'windows-x64':
        obs = work / 'obs-studio'
        with zipfile.ZipFile(archive) as z:
            z.extractall(obs)
        variables['AWL_OBS_DIR'] = str(obs)
    else:
        mount = work / 'obs-mount'
        subprocess.run(['hdiutil', 'attach', str(archive), '-nobrowse', '-readonly',
                        '-mountpoint', str(mount)], check=True)
        try:
            subprocess.run(['sudo', 'ditto', str(mount / 'OBS.app'), '/Applications/OBS.app'], check=True)
        finally:
            subprocess.run(['hdiutil', 'detach', str(mount)], check=True)
        source = json.loads((ROOT / 'chat_plugin/dependencies-windows.lock.json').read_text())['obs_source']
        fetch(source, work / 'obs-source.tar.gz')
        with tarfile.open(work / 'obs-source.tar.gz') as tar:
            tar.extractall(work, filter='data')
        variables['AWL_OBS_SOURCE'] = str(work / 'obs-studio-32.2.2')
    with open(os.environ['GITHUB_ENV'], 'a', encoding='utf-8') as environment:
        for key, value in variables.items():
            environment.write(f'{key}={value}\n')


if __name__ == '__main__':
    main()
