"""Check translator coverage and substitution parity without network access."""
import json
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
entries = json.loads((root / 'data/locale/ru-RU.json').read_text(encoding='utf-8'))
literal = r'"(?:[^"\\]|\\.)*"'
marked = re.compile(r'ui_text\(\s*(' + literal + r'(?:\s*' + literal + r')*)\s*\)')
used = set()
for path in (root / 'src').glob('*.cpp'):
    for match in marked.finditer(path.read_text(encoding='utf-8')):
        source = ''.join(json.loads(part) for part in re.findall(literal, match[1]))
        assert source in entries, f'{path.name}: missing Russian translation for {source!r}'
        used.add(source)
for source, translation in entries.items():
    assert isinstance(translation, str) and translation.strip(), f'Empty translation: {source}'
    assert sorted(re.findall(r'%\d+', source)) == sorted(re.findall(r'%\d+', translation)), source
    assert source.count('\n') == translation.count('\n'), f'Paragraph structure changed: {source}'
assert len(used) > 40, 'Expected UI translation markers were not found'
print(f'{len(entries)} non-empty Russian entries; {len(used)} marked UI strings covered; placeholders and paragraphs match.')
