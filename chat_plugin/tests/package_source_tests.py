"""Release source must exclude local secrets and disclose dirty checkouts."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
import package_source


class SourceArchiveTests(unittest.TestCase):
    def test_only_tracked_source_and_clean_ci(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            def git(*args):
                subprocess.run(['git', *args], cwd=root, check=True, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL)
            git('init')
            (root / '.gitignore').write_text('.env\ncache/\n')
            (root / 'source.cpp').write_text('source to review\n')
            git('add', '.')
            git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                'commit', '-m', 'Fixture')
            (root / '.env').write_text('SECRET=must-not-be-archived\n')
            cache = root / 'cache'
            cache.mkdir()
            output = cache / 'source.zip'
            with patch.object(package_source, 'ROOT', root), patch.dict(os.environ, {'GITHUB_ACTIONS': 'true'}):
                info = package_source.write_source(output, cache, [])
                self.assertFalse(info['dirty'])
                with zipfile.ZipFile(output) as archive:
                    self.assertEqual(set(archive.namelist()), {'.gitignore', 'source.cpp', 'build-source.json'})
                (root / 'source.cpp').write_text('uncommitted change\n')
                with self.assertRaisesRegex(RuntimeError, 'clean source checkout'):
                    package_source.write_source(output, cache, [])
                git('checkout', '--', 'source.cpp')
                git('add', '--force', '.env')
                git('-c', 'user.name=Fixture', '-c', 'user.email=fixture@example.invalid',
                    'commit', '-m', 'Accidentally tracked secret')
                with self.assertRaisesRegex(RuntimeError, 'Environment files'):
                    package_source.write_source(output, cache, [])


if __name__ == '__main__':
    unittest.main()
