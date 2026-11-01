"""Check that the Info screen's metadata path reads FB2 annotations without rendering the book."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile
import shutil
import xml.etree.ElementTree as ET

exe = Path(sys.argv[1]).resolve()
xml = '''<?xml version="1.0" encoding="utf-8"?>
<FictionBook><description><title-info><author><first-name>Иван</first-name><last-name>Тестов</last-name></author>
<book-title>Название</book-title><annotation><p>Первый <strong>абзац</strong>.</p>
<p>Второй абзац.</p></annotation><sequence name="Серия" number="2"/></title-info></description>
<body><section><p>Текст книги.</p></section></body></FictionBook>'''
with tempfile.TemporaryDirectory(prefix='fb2-annotation-') as tmp:
    root = Path(tmp)
    env = dict(os.environ, FB2_TEST_SD=str(root))
    (root / 'book.fb2').write_text(xml, encoding='utf-8')
    with zipfile.ZipFile(root / 'book.fb2.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        archive.writestr('book.fb2', xml.encode('utf-8'))
    for source in ('/book.fb2', '/book.fb2.zip'):
        caches_before = set(root.rglob('fb2-reader-*'))
        result = subprocess.run([str(exe), '--metadata', source], env=env,
                                capture_output=True, text=True, encoding='utf-8', check=True)
        assert result.stdout.splitlines() == [
            'Название', 'Иван Тестов', 'Серия', '2', 'Первый абзац.', 'Второй абзац.'
        ], result.stdout
        assert set(root.rglob('fb2-reader-*')) == caches_before, 'Metadata lookup created a reader cache'
        cache_result = subprocess.run([str(exe), '--reader-cache', source], env=env,
                                      capture_output=True, text=True, encoding='utf-8', check=True)
        cache = root / cache_result.stdout.splitlines()[0].lstrip('/')
        chapter = ET.parse(cache / 'c0.xhtml').getroot()
        body = chapter.find('{http://www.w3.org/1999/xhtml}body')
        assert body is not None
        assert body[0].get('class') == 'fb2-annotation'
        assert ''.join(body[0].itertext()) == 'Первый абзац.Второй абзац.'
        assert 'Текст книги.' in ''.join(body.itertext())

        if source == '/book.fb2':
            old_cache = Path(str(cache).replace('fb2-reader-v4-', 'fb2-reader-v3-'))
            old_cache.mkdir(parents=True)
            (old_cache / 'progress.bin').write_bytes(b'previous-progress')
            (old_cache / 'bookmarks.bin').write_bytes(b'previous-bookmarks')
            shutil.rmtree(cache)
            subprocess.run([str(exe), '--reader-cache', source], env=env, check=True,
                           capture_output=True)
            assert (cache / 'progress.bin').read_bytes() == b'previous-progress'
            assert (cache / 'bookmarks.bin').read_bytes() == b'previous-bookmarks'
            assert (old_cache / 'progress.bin').read_bytes() == b'previous-progress'
print('PASS: FB2 and FB2.ZIP annotation in first reading chapter; old reading state preserved')
