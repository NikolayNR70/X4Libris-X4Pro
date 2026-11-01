"""Check that the Info screen's metadata path reads FB2 annotations without rendering the book."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile

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
        result = subprocess.run([str(exe), '--metadata', source], env=env,
                                capture_output=True, text=True, encoding='utf-8', check=True)
        assert result.stdout.splitlines() == [
            'Название', 'Иван Тестов', 'Серия', '2', 'Первый абзац.', 'Второй абзац.'
        ], result.stdout
        assert not list(root.rglob('fb2-reader-*')), 'Metadata lookup created a reader cache'
print('PASS: FB2 and FB2.ZIP annotation metadata')
