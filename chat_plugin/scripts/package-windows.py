#!/usr/bin/env python3
"""Stage a model-free, self-contained Windows OBS plugin and source archive.

Run inside a Visual Studio x64 developer environment (dumpbin checks).
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'chat_plugin'
DEPS = ROOT / '.phase0/chat/deps-win'
BUILD = ROOT / '.phase0/chat/build-win'
DESTINATION = ROOT / '.phase0/packages/obs-chat-win'
CMAKE = os.environ.get('CMAKE') or shutil.which('cmake') or 'cmake'


def dependents(binary):
    output = subprocess.check_output(['dumpbin', '/nologo', '/dependents', str(binary)], text=True)
    return {name.lower() for name in re.findall(r'^\s+(\S+\.dll)\s*$', output, re.MULTILINE | re.IGNORECASE)}


def main():
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    stage_parent = Path(tempfile.mkdtemp(prefix='.chat-stage-', dir=DESTINATION.parent))
    STAGE = stage_parent / 'awl-chat'
    subprocess.run([CMAKE, '--install', str(BUILD), '--prefix', str(stage_parent)], check=True)
    binaries = STAGE / 'bin/64bit'
    data = STAGE / 'data'
    module, helper = binaries / 'awl-chat.dll', binaries / 'awl-chat-worker.exe'
    # The helper loads inference DLLs from its own directory. OBS loads the extra
    # Qt module beside the plugin DLL; base Qt DLLs are OBS's own matching copies.
    for name in ['ctranslate2.dll', 'libiomp5md.dll', 'cudnn64_9.dll']:
        shutil.copy2(DEPS / 'bin' / name, binaries)
    shutil.copy2(DEPS / 'qt/bin/Qt6WebSockets.dll', binaries)
    tls = binaries / 'qt-plugins/tls'
    tls.mkdir(parents=True)
    shutil.copy2(DEPS / 'qt/plugins/tls/qschannelbackend.dll', tls)
    for name in ['README.md', 'ARCHITECTURE.md', 'ISOLATION.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md',
                 'dependencies.lock.json', 'dependencies-windows.lock.json']:
        shutil.copy2(PROJECT / name, data)
    with zipfile.ZipFile(data / 'source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(PROJECT.rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:
                z.write(p, str(p.relative_to(ROOT)))
        for name in ['Install Awl Interpretwitch.command', 'README.md', 'BUILD.md', 'LICENSE', '.gitignore']:
            z.write(ROOT / name, name)
        # QtWebSockets corresponding source is shipped alongside the library.
        for name in ['qtwebsockets', 'ctranslate2', 'sentencepiece', 'fasttext']:
            z.write(ROOT / '.phase0/chat/archives-win' / name, 'dependency-sources/' + name + '.tar.gz')
    files = [p for p in STAGE.rglob('*') if p.is_file()]
    if any(p.suffix in ('.bin', '.ftz', '.spm', '.model') for p in files):
        raise RuntimeError('Model weights unexpectedly bundled')
    if any(p.is_symlink() for p in STAGE.rglob('*')):
        raise RuntimeError('Unexpected package symlink')
    linked = dependents(module)
    if 'ctranslate2.dll' in linked or 'winhttp.dll' in linked:
        raise RuntimeError('Inference/download runtime leaked into the OBS module')
    if not {'obs.dll', 'qt6core.dll', 'qt6websockets.dll'} <= linked:
        raise RuntimeError(f'Unexpected OBS module imports: {sorted(linked)}')
    helper_linked = dependents(helper)
    if 'ctranslate2.dll' not in helper_linked or any(n.startswith('qt6') and n != 'qt6core.dll' for n in helper_linked):
        raise RuntimeError(f'Unexpected helper imports: {sorted(helper_linked)}')
    # Every non-system DLL the package imports must be shipped or provided by OBS.
    obs_bin = Path(os.environ.get('AWL_OBS_DIR', r'C:\Program Files\obs-studio')) / 'bin/64bit'
    for binary in [module, helper, binaries / 'Qt6WebSockets.dll', tls / 'qschannelbackend.dll']:
        for name in dependents(binary):
            if name.startswith(('qt6', 'obs')) and not (binaries / name).exists() and not (obs_bin / name).exists():
                raise RuntimeError(f'{binary.name} needs {name}, which neither the package nor OBS provides')
    version = re.search(r'project\(awl-chat VERSION (\S+)', (PROJECT / 'CMakeLists.txt').read_text()).group(1)
    evidence = {'version': version,
                'bytes': sum(p.stat().st_size for p in files),
                'module_sha256': hashlib.sha256(module.read_bytes()).hexdigest(),
                'helper_sha256': hashlib.sha256(helper.read_bytes()).hexdigest(),
                'source_sha256': hashlib.sha256((data / 'source.zip').read_bytes()).hexdigest(),
                'inference_process': 'awl-chat-worker.exe',
                'models_bundled': False, 'signature': 'unsigned development build'}
    (stage_parent / 'package.json').write_text(json.dumps(evidence, indent=2) + '\n')
    if DESTINATION.exists():
        backup = Path(tempfile.mkdtemp(prefix='.chat-previous-', dir=DESTINATION.parent)) / 'package'
        DESTINATION.rename(backup)
    stage_parent.rename(DESTINATION)
    print(DESTINATION / 'awl-chat')


if __name__ == '__main__':
    main()
