#!/usr/bin/env python3
"""Stage a model-free, self-contained native Mac plugin and source archive."""
import hashlib
import json
import os
import plistlib
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[2]
PROJECT = ROOT / 'chat_plugin'
DEPS = ROOT / '.phase0/chat/deps'
DESTINATION = ROOT / '.phase0/packages/obs-chat-mac'
CMAKE = os.environ.get('CMAKE') or shutil.which('cmake') or str(ROOT / '.phase0/toolchain/cmake/data/bin/cmake')


def main():
    DESTINATION.parent.mkdir(parents=True, exist_ok=True)
    stage_parent = Path(tempfile.mkdtemp(prefix='.chat-stage-', dir=DESTINATION.parent))
    STAGE = stage_parent / 'awl-chat.plugin'
    subprocess.run([CMAKE, '--install', str(ROOT / '.phase0/chat/build'),
                    '--prefix', str(STAGE.parent)], check=True)
    frameworks = STAGE / 'Contents/Frameworks'
    frameworks.mkdir(exist_ok=True)
    shutil.copy2(DEPS / 'lib/libawl-ctranslate2.dylib', frameworks)
    # Ship the extra Qt module as a private dylib, using OBS's existing base Qt
    # frameworks. Avoid framework symlinks and a duplicate QtCore/Widgets copy.
    ws = frameworks / 'libawl-qtwebsockets.dylib'
    shutil.copy2(DEPS / 'qt/lib/QtWebSockets.framework/Versions/A/QtWebSockets', ws)
    subprocess.run(['install_name_tool', '-id', '@rpath/libawl-qtwebsockets.dylib', str(ws)], check=True)
    subprocess.run(['install_name_tool', '-change', '@rpath/QtWebSockets.framework/Versions/A/QtWebSockets',
                    '@rpath/libawl-qtwebsockets.dylib', str(STAGE / 'Contents/MacOS/awl-chat')], check=True)
    resources = STAGE / 'Contents/Resources'
    for name in ['README.md', 'ARCHITECTURE.md', 'ISOLATION.md', 'INLINE_CHAT.md', 'LICENSE', 'THIRD_PARTY_NOTICES.md', 'dependencies.lock.json']:
        shutil.copy2(PROJECT / name, resources)
    with zipfile.ZipFile(resources / 'source.zip', 'w', zipfile.ZIP_DEFLATED) as z:
        for p in sorted(PROJECT.rglob('*')):
            if p.is_file() and '__pycache__' not in p.parts:
                z.write(p, str(p.relative_to(ROOT)))
        for name in ['Install Interpretwitch.command', 'README.md', 'BUILD.md', 'LICENSE', '.gitignore']:
            z.write(ROOT / name, name)
        # QtWebSockets corresponding source is shipped alongside the library.
        for name in ['qtwebsockets', 'ctranslate2', 'sentencepiece', 'fasttext', 'simde']:
            p = ROOT / '.phase0/chat/archives' / name
            z.write(p, 'dependency-sources/' + name + '.tar.gz')
    shutil.copy2(DEPS / 'qt/lib/QtWebSockets.framework/Versions/A/Resources/PrivacyInfo.xcprivacy', resources / 'QtWebSockets-PrivacyInfo.xcprivacy')
    helper = STAGE / 'Contents/MacOS/awl-chat-worker'
    for binary in [frameworks / 'libawl-ctranslate2.dylib', ws, helper]:
        subprocess.run(['lipo', str(binary), '-verify_arch', 'arm64'], check=True)
        subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(binary)], check=True)
    subprocess.run(['codesign', '--force', '--sign', '-', '--timestamp=none', str(STAGE)], check=True)
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(STAGE)], check=True)
    files = [p for p in STAGE.rglob('*') if p.is_file()]
    if any(p.suffix in ('.bin', '.ftz', '.spm', '.model') for p in files):
        raise RuntimeError('Model weights unexpectedly bundled')
    if any(p.is_symlink() for p in STAGE.rglob('*')):
        raise RuntimeError('Unexpected package symlink')
    linked = subprocess.check_output(['otool', '-L', str(STAGE / 'Contents/MacOS/awl-chat')], text=True)
    if 'ctranslate2' in linked or 'libcurl' in linked:
        raise RuntimeError('Inference/download runtime leaked into the OBS module')
    helper_load = subprocess.check_output(['otool', '-l', str(helper)], text=True)
    if any(str(ROOT / '.phase0') in line and line.strip().startswith(('path ', 'name '))
           for line in helper_load.splitlines()):
        raise RuntimeError('Helper still depends on developer runtime paths')
    version = plistlib.loads((STAGE / 'Contents/Info.plist').read_bytes())['CFBundleShortVersionString']
    evidence = {'product': 'Interpretwitch', 'publisher': 'Salvador Stream Labs', 'version': version, 'bytes': sum(p.stat().st_size for p in files),
                'module_sha256': hashlib.sha256((STAGE / 'Contents/MacOS/awl-chat').read_bytes()).hexdigest(),
                'helper_sha256': hashlib.sha256(helper.read_bytes()).hexdigest(),
                'source_sha256': hashlib.sha256((resources / 'source.zip').read_bytes()).hexdigest(),
                'inference_process': 'awl-chat-worker',
                'models_bundled': False, 'signature': 'local ad-hoc, not notarized'}
    (stage_parent / 'package.json').write_text(json.dumps(evidence, indent=2) + '\n')
    if DESTINATION.exists():
        backup = Path(tempfile.mkdtemp(prefix='.chat-previous-', dir=DESTINATION.parent)) / 'package'
        DESTINATION.rename(backup)
    stage_parent.rename(DESTINATION)
    print(DESTINATION / 'awl-chat.plugin')


if __name__ == '__main__':
    main()
