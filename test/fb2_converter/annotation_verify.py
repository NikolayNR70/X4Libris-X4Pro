"""Check FB2 annotation metadata and styled reading text for plain and ZIP books."""
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
<FictionBook><stylesheet type="text/css">.anno { font-style: italic; }</stylesheet>
<description><title-info><author><first-name>Иван</first-name><last-name>Тестов</last-name></author>
<book-title>Название</book-title><annotation><p class="anno">Первый <strong>абзац</strong>.</p>
<p style="text-align: center">Второй <emphasis>абзац</emphasis>.</p></annotation>
<sequence name="Серия" number="2"/></title-info></description>
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
        assert ''.join(''.join(body[0].itertext()).split()) == 'Первыйабзац.Второйабзац.'
        ns = '{http://www.w3.org/1999/xhtml}'
        paragraphs = body[0].findall(ns + 'p')
        assert paragraphs[0].get('class') == 'anno'
        assert paragraphs[0].find(ns + 'strong') is not None
        assert paragraphs[1].get('style') == 'text-align: center'
        assert paragraphs[1].find(ns + 'em') is not None
        assert '.anno { font-style: italic; }' in (cache / 'fb2.css').read_text(encoding='utf-8')
        assert 'Текст книги.' in ''.join(body.itertext())

        if source == '/book.fb2':
            old_cache = Path(str(cache).replace('fb2-reader-v5-', 'fb2-reader-v4-'))
            old_cache.mkdir(parents=True)
            (old_cache / 'progress.bin').write_bytes(b'previous-progress')
            (old_cache / 'bookmarks.bin').write_bytes(b'previous-bookmarks')
            older_cache = Path(str(cache).replace('fb2-reader-v5-', 'fb2-reader-v3-'))
            older_cache.mkdir(parents=True)
            (older_cache / 'progress.bin').write_bytes(b'stale-progress')
            shutil.rmtree(cache)
            subprocess.run([str(exe), '--reader-cache', source], env=env, check=True,
                           capture_output=True)
            assert (cache / 'progress.bin').read_bytes() == b'previous-progress'
            assert (cache / 'bookmarks.bin').read_bytes() == b'previous-bookmarks'
            assert (old_cache / 'progress.bin').read_bytes() == b'previous-progress'
print('PASS: styled FB2 and FB2.ZIP annotation in first chapter; latest reading state preserved')
