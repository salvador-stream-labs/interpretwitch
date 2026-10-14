"""Inspect the real PKG locally; exercise installation only on a disposable CI Mac."""
import argparse
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import xml.etree.ElementTree as ET

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from package_source import ROOT, sha256

PACKAGE = ROOT / '.phase0/packages/obs-chat-mac'
RELATIVE = Path('Library/Application Support/obs-studio/plugins/awl-chat.plugin')


def check(value, message):
    if not value:
        raise RuntimeError(message)


def run(*args):
    return subprocess.check_output(list(map(str, args)), text=True)


def inspect(installer, evidence):
    with tempfile.TemporaryDirectory(prefix='interpretwitch-pkg-') as temporary:
        expanded = Path(temporary) / 'expanded'
        run('pkgutil', '--expand-full', installer, expanded)
        dist = ET.parse(expanded / 'Distribution').getroot()
        check(dist.find('domains').attrib == {'enable_anywhere': 'false',
              'enable_currentUserHome': 'true', 'enable_localSystem': 'false'}, 'Unexpected install domain')
        check(dist.find('options').get('hostArchitectures') == 'arm64', 'Wrong architecture')
        check(dist.find('volume-check/allowed-os-versions/os-version').get('min') == '14.0', 'Wrong minimum OS')
        component = expanded / 'Interpretwitch-component.pkg'
        info = ET.parse(component / 'PackageInfo').getroot()
        check(info.get('install-location') == '/' and info.get('version') == evidence['version'], 'Wrong payload destination/version')
        check(info.get('relocatable') == 'false' and not info.findall('relocate/bundle'),
              'Installer must not search for and modify other plugin copies')
        check(info.find('upgrade-bundle/bundle').get('id') == 'org.awl.chat', 'Installer must replace the whole bundle')
        bundle = component / 'Payload' / RELATIVE
        for key, relative in (('module_sha256', 'Contents/MacOS/awl-chat'),
                              ('helper_sha256', 'Contents/MacOS/awl-chat-worker'),
                              ('source_sha256', 'Contents/Resources/source.zip')):
            check(sha256(bundle / relative) == evidence[key], f'PKG payload differs: {relative}')
        check(all(path.is_relative_to(bundle) for path in (component / 'Payload').rglob('*') if path.is_file()),
              'Payload writes files outside the plugin bundle')
        check(not any(path.is_symlink() for path in expanded.rglob('*')), 'Unexpected symlink')
        check(os.access(bundle / 'Contents/MacOS/awl-chat-worker', os.X_OK), 'Helper lost executable permission')
        run('codesign', '--verify', '--deep', '--strict', bundle)
        for locale in ('en', 'ru'):
            for name in ('welcome.txt', 'conclusion.txt', 'Localizable.strings'):
                check((expanded / 'Resources' / (locale + '.lproj') / name).is_file(), 'Missing installer translation')
        # A redirected plugin/backup folder must be rejected before any writes.
        user = Path(temporary) / 'fixture-user'
        user.mkdir()
        common = component / 'Scripts/mac-install-common.sh'
        command = 'source "$1"; mac_install_paths "$2"'
        run('/bin/zsh', '-c', command, 'fixture', common, user)
        check(not (user / 'Library').exists(), 'Preflight created folders')
        plugins = user / RELATIVE.parent
        plugins.parent.mkdir(parents=True)
        outside = Path(temporary) / 'outside'
        outside.mkdir()
        plugins.symlink_to(outside, target_is_directory=True)
        result = subprocess.run(['/bin/zsh', '-c', command, 'fixture', str(common), str(user)])
        check(result.returncode != 0 and not list(outside.iterdir()), 'Preflight followed a symlink')
    domains = run('/usr/sbin/installer', '-dominfo', '-pkg', installer)
    check('CurrentUserHomeDirectory' in domains and 'LocalSystem' not in domains, 'Installer offered a system-wide domain')
    print('PKG payload, signatures, destinations, English/Russian resources and preflight checks passed.')


def install(installer):
    check(os.environ.get('GITHUB_ACTIONS') == 'true', 'Install tests require a disposable GitHub runner')
    target = Path.home() / RELATIVE
    check(not target.exists(), 'Refusing to replace an existing plugin during testing')
    settings = target.parent.parent / 'plugin_config/awl-chat'
    settings.mkdir(parents=True, exist_ok=True)
    marker = settings / 'installer-fixture.txt'
    marker.write_text('Keep settings and models')
    command = ['/usr/sbin/installer', '-pkg', installer, '-target', 'CurrentUserHomeDirectory']
    run(*command)
    run('codesign', '--verify', '--deep', '--strict', target)
    (target / 'obsolete-fixture.txt').write_text('Previous plugin')
    run(*command)
    check(not (target / 'obsolete-fixture.txt').exists(), 'Upgrade merged old files into the new bundle')
    backups = target.parent.parent / 'awl-plugin-backups'
    check(any(backups.glob('chat.*/awl-chat.plugin/obsolete-fixture.txt')), 'Upgrade lost the previous plugin')
    run('codesign', '--verify', '--deep', '--strict', target)
    shutil.rmtree(target)  # Documented Mac removal: remove only the plugin bundle.
    check(marker.read_text() == 'Keep settings and models', 'Installation/removal changed user data')
    print('Current-user fresh install, upgrade, backup and removal passed.')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--install', action='store_true')
    args = parser.parse_args()
    evidence = json.loads((PACKAGE / 'package.json').read_text())
    installer = PACKAGE / f"Interpretwitch-{evidence['version']}-macos-arm64.pkg"
    inspect(installer, evidence)
    if args.install:
        install(installer)
