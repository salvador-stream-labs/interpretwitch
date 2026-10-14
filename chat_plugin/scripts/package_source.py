"""Archive tracked source only and record which checkout produced a package."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import zipfile

ROOT = Path(__file__).resolve().parents[2]


def source_info():
    commit = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
    dirty = bool(subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=normal'], cwd=ROOT))
    if os.environ.get('GITHUB_ACTIONS') == 'true' and dirty:
        raise RuntimeError('CI packages require a clean source checkout')
    return {'commit': commit, 'dirty': dirty,
            'repository': 'https://github.com/salvador-stream-labs/interpretwitch',
            'build_url': (f"https://github.com/{os.environ['GITHUB_REPOSITORY']}/actions/runs/"
                          f"{os.environ['GITHUB_RUN_ID']}" if os.environ.get('GITHUB_RUN_ID') else None)}


def write_source(destination, archives, dependencies):
    info = source_info()
    paths = subprocess.check_output(['git', 'ls-files', '-z'], cwd=ROOT).decode().split('\0')
    with zipfile.ZipFile(destination, 'w', zipfile.ZIP_DEFLATED) as archive:
        for name in sorted(filter(None, paths)):
            path = ROOT / name
            if path.is_symlink() or not path.is_file():
                raise RuntimeError(f'Unexpected tracked source path: {name}')
            if path.name == '.env' or path.name.startswith('.env.') and path.name != '.env.example':
                raise RuntimeError('Environment files cannot be packaged')
            archive.write(path, name)
        for name in dependencies:
            archive.write(archives / name, 'dependency-sources/' + name + '.tar.gz')
        archive.writestr('build-source.json', json.dumps(info, indent=2) + '\n')
    return info


def sha256(path):
    with Path(path).open('rb') as file:
        return hashlib.file_digest(file, 'sha256').hexdigest()
