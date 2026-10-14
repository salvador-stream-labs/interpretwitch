"""Build a current-user-only Installer package from the checked Mac bundle."""
import json
import plistlib
from pathlib import Path
import shutil
import subprocess
import tempfile
from package_source import ROOT, sha256, source_info

PACKAGE = ROOT / '.phase0/packages/obs-chat-mac'
SCRIPTS = ROOT / 'chat_plugin/scripts'


def build(package=PACKAGE):
    bundle = package / 'awl-chat.plugin'
    evidence = json.loads((package / 'package.json').read_text())
    if evidence['source'] != source_info():
        raise RuntimeError('Rebuild the plugin before creating its installer')
    for key, relative in (('module_sha256', 'Contents/MacOS/awl-chat'),
                          ('helper_sha256', 'Contents/MacOS/awl-chat-worker'),
                          ('source_sha256', 'Contents/Resources/source.zip')):
        if sha256(bundle / relative) != evidence[key]:
            raise RuntimeError(f'Packaged file changed: {relative}')
    if bundle.is_symlink() or any(path.is_symlink() for path in bundle.rglob('*')):
        raise RuntimeError('Installer bundle contains a symlink')
    subprocess.run(['codesign', '--verify', '--deep', '--strict', str(bundle)], check=True)
    version = evidence['version']
    output = package / f'Interpretwitch-{version}-macos-arm64.pkg'
    with tempfile.TemporaryDirectory(prefix='.installer-', dir=package) as temporary:
        work = Path(temporary)
        root = work / 'root'
        relative = Path('Library/Application Support/obs-studio/plugins/awl-chat.plugin')
        shutil.copytree(bundle, root / relative)
        scripts = work / 'scripts'
        scripts.mkdir()
        shutil.copy2(SCRIPTS / 'mac-install-common.sh', scripts)
        shutil.copy2(SCRIPTS / 'macos/preinstall', scripts)
        (scripts / 'preinstall').chmod(0o755)
        components = work / 'components.plist'
        components.write_bytes(plistlib.dumps([{
            'RootRelativeBundlePath': relative.as_posix(),
            'BundleIsRelocatable': False,
            'BundleIsVersionChecked': False,
            'BundleHasStrictIdentifier': True,
            'BundleOverwriteAction': 'upgrade',
        }]))
        subprocess.run(['pkgbuild', '--root', str(root), '--component-plist', str(components),
                        '--scripts', str(scripts), '--identifier', 'org.salvadorstreamlabs.interpretwitch',
                        '--version', version, '--install-location', '/', '--ownership', 'recommended',
                        str(work / 'Interpretwitch-component.pkg')], check=True)
        subprocess.run(['productbuild', '--distribution', str(SCRIPTS / 'macos/Distribution.xml'),
                        '--resources', str(SCRIPTS / 'macos/resources'), '--package-path', str(work),
                        '--version', version, str(work / output.name)], check=True)
        (work / output.name).replace(output)
    print(output)
    return output


if __name__ == '__main__':
    build()
