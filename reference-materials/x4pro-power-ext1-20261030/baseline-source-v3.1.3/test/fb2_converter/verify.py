"""Exercise the production converter and XML parser against a host SD-card adapter.

Usage: python verify.py path/to/fb2-test.exe scratch-directory
"""
import base64
import hashlib
import os
from pathlib import Path
import subprocess
import sys
import zipfile
import xml.etree.ElementTree as ET

exe = Path(sys.argv[1]).resolve()
sd = Path(sys.argv[2]).resolve()
sd.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, FB2_TEST_SD=str(sd))
png = base64.b64decode('iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=')
template = '''<?xml version="1.0" encoding="ENCODING"?>
<FictionBook xmlns="http://www.gribuser.ru/xml/fictionbook/2.0" xmlns:l="http://www.w3.org/1999/xlink">
<description><title-info><genre>prose</genre><author><first-name>Иван</first-name><last-name>Тестов</last-name></author>
<book-title>Проверка &amp; книга</book-title><lang>ru</lang><coverpage><image l:href="#cover"/></coverpage></title-info></description>
<stylesheet type="text/css">.reader-test { text-align: right; }</stylesheet>
<body><title><p>Предисловие</p></title><section id="one"><title><p>Первая глава</p></title>
<p class="reader-test" style="font-weight: bold">Привет, ёж! &amp; &lt;мир&gt; <strong>жирный</strong> <emphasis>курсив</emphasis> <a l:href="#note" type="note">1</a></p>
<image l:href="#cover"/><section id="nested"><title><p>Вложенный раздел</p></title><p>Текст вложенного раздела.</p></section></section>
<section id="two"><title><p>Вторая глава</p></title><p>Конец.</p></section></body>
<body name="notes"><section id="note"><title><p>1</p></title><p>Сноска <a l:href="#one">назад</a></p></section></body>
<binary id="cover" content-type="image/png">IMAGE</binary></FictionBook>'''

def run(name, data, success=True):
    source = sd / name
    source.write_bytes(data)
    before = hashlib.sha256(data).hexdigest()
    result = subprocess.run([str(exe), '/'+name], env=env, capture_output=True, text=True)
    assert (result.returncode == 0) == success, (name, result.stdout, result.stderr)
    assert hashlib.sha256((source if source.exists() else source.with_name("."+source.name)).read_bytes()).hexdigest() == before
    if not success:
        assert result.stderr, name
        return
    output, original = result.stdout.splitlines()
    assert original == output
    assert output == '/'+str(Path(name).with_suffix('.epub')).replace('\\', '/')
    path = sd / output.lstrip('/')
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        assert z.infolist()[0].filename == 'mimetype'
        assert z.infolist()[0].compress_type == 0
        assert z.read('mimetype') == b'application/epub+zip'
        for item in z.namelist():
            if item.endswith(('.xml', '.opf', '.ncx', '.xhtml')):
                ET.fromstring(z.read(item))
        opf = ET.fromstring(z.read('content.opf'))
        ns = {'o':'http://www.idpf.org/2007/opf', 'dc':'http://purl.org/dc/elements/1.1/'}
        assert opf.find('.//dc:title', ns).text == 'Проверка & книга'
        assert opf.find('.//dc:creator', ns).text == 'Иван Тестов'
        assert len(opf.findall('.//o:itemref', ns)) == 4
        assert z.read('i0.png') == png
        html = {n:ET.fromstring(z.read(n)) for n in z.namelist() if n.endswith('.xhtml')}
        text = ''.join(''.join(root.itertext()) for root in html.values())
        assert 'Привет, ёж!' in text and 'Вложенный раздел' in text and 'Сноска' in text
        for root in html.values():
            for a in root.iter('{http://www.w3.org/1999/xhtml}a'):
                target, anchor = a.attrib['href'].split('#')
                assert any(e.attrib.get('id') == anchor for e in html[target].iter())
    stamp = path.stat().st_mtime_ns
    source.write_bytes(data)  # A new upload of the same FB2 must not replace the EPUB.
    second = subprocess.run([str(exe), '/'+name], env=env, capture_output=True, text=True)
    assert second.returncode != 0
    assert path.stat().st_mtime_ns == stamp, 'Existing EPUB was rewritten'
    return output

for encoding in ('utf-8', 'windows-1251', 'utf-16'):
    xml = template.replace('ENCODING', encoding).replace('IMAGE', base64.b64encode(png).decode())
    output = run(encoding+'.fb2', xml.encode(encoding))
    print('PASS:', encoding, 'text, metadata, chapters, image, cross-chapter notes, sibling EPUB, original preserved')
    modified = xml.replace('Конец.', 'Другой конец.').encode(encoding)
    run(encoding+'.fb2', modified, success=False)
    print('PASS: changed source cannot replace existing EPUB')

valid = template.replace('ENCODING', 'utf-8').replace('IMAGE', base64.b64encode(png).decode())
for label, data in (
    ('truncated', valid[:-20].encode()),
    ('wrong-root', b'<html><body><p>hello</p></body></html>'),
    ('bad-base64', valid.replace(base64.b64encode(png).decode(), '@@@').encode()),
    ('duplicate-id', valid.replace('id="two"', 'id="one"').encode()),
    ('empty', b''),
    ('unsupported-encoding', valid.replace('encoding="utf-8"', 'encoding="iso-8859-1"').encode()),
    ('chapter-limit', ('<FictionBook><body>'+'<section><p>x</p></section>'*513+'</body></FictionBook>').encode()),
    ('deep', ('<FictionBook><body>'+'<section>'*70+'<p>x</p>'+'</section>'*70+'</body></FictionBook>').encode()),
):
    run(label+'.fb2', data, success=False)
    print('PASS: rejects', label)

env['FB2_TEST_FAIL_WRITE'] = '1'
run('sd-write-error.fb2', valid.encode(), success=False)
env.pop('FB2_TEST_FAIL_WRITE')
run('sd-write-error.fb2', valid.encode())
print('PASS: SD write failure cleans temporary files; retry succeeds')
assert not list(sd.rglob('*.part')), 'Partial EPUB leaked'
print('All FB2 converter checks passed.')

# Publication, readback and reversible hiding failures.
for fault, success in [('FAIL_RENAME', False), ('CORRUPT_STAGE', False), ('CORRUPT_FINAL', False), ('RACE', False), ('FAIL_HIDE', True)]:
    name = fault + '.Fb2'
    env['FB2_TEST_' + fault] = '1'
    run(name, valid.encode(), success=success)
    env.pop('FB2_TEST_' + fault)
    assert (sd/name).read_bytes() == valid.encode()
    if fault == 'RACE':
        assert (sd/Path(name).with_suffix('.epub')).read_bytes() == b'racing upload'
    print('PASS:', fault)
# Existing backup must survive; nested mixed-case FB2 produces its sibling.
(sd/'nested').mkdir(exist_ok=True)
(sd/'nested'/'.book.Fb2').write_bytes(b'old backup')
run('nested/book.Fb2', valid.encode())
assert (sd/'nested'/'.book.Fb2').read_bytes() == b'old backup'
assert (sd/'nested'/'book.Fb2').exists()
assert not list(sd.rglob('*.part'))
print('Publication and recovery checks passed.')

name = 'changing.fb2'
(sd/name).write_bytes(valid.encode())
env['FB2_TEST_SOURCE_CHANGED'] = '1'
r = subprocess.run([str(exe), '/'+name], env=env, capture_output=True)
env.pop('FB2_TEST_SOURCE_CHANGED')
assert r.returncode == 0
assert (sd/name).read_bytes() == valid.encode() + b'changed source'
assert not (sd/('.'+name)).exists()
print('PASS: source changed during publication stays visible')
# A pre-v1.1 cache is unrelated to staging and must remain intact.
legacy = sd / '.crosspoint' / 'fb2-v2-existing'
legacy.mkdir(parents=True)
(legacy/'book.epub').write_bytes(b'legacy book')
run('legacy-preserved.fb2', valid.encode())
assert (legacy/'book.epub').read_bytes() == b'legacy book'
print('PASS: legacy cache remains intact')

# Direct reader preparation writes only private render cache data: the original FB2 remains
# byte-for-byte at its original path, no sibling EPUB appears, and semantic formatting survives.
name = 'direct-reader.fb2'
source = sd / name
source.write_bytes(valid.encode())
before = hashlib.sha256(source.read_bytes()).digest()
r = subprocess.run([str(exe), '--reader-cache', '/' + name], env=env, capture_output=True, text=True)
assert r.returncode == 0, (r.stdout, r.stderr)
lines = r.stdout.splitlines()
assert lines[0].startswith('/.crosspoint/tmp/fb2-reader-v3-'), lines
assert lines[1:4] == ['Проверка & книга', 'Иван Тестов', 'ru'], lines
chapter_count, image_count = int(lines[4]), int(lines[5])
cache = sd / lines[0].lstrip('/')
assert chapter_count >= 3 and image_count == 1, lines
paths = subprocess.check_output([str(exe), '--reader-paths', '/' + name], env=env, text=True).splitlines()
assert len(paths) == chapter_count and all(path.startswith('/FictionBook[1]/body[') for path in paths), paths
assert paths[1] == '/FictionBook[1]/body[1]/section[1]' and paths[2] == '/FictionBook[1]/body[1]/section[2]', paths
assert source.read_bytes() and hashlib.sha256(source.read_bytes()).digest() == before
assert not (sd / 'direct-reader.epub').exists()
assert not (sd / ('.' + name)).exists()
assert (cache / 'reader.meta').is_file() and (cache / 'fb2.css').is_file()
assert not any(p.name.startswith('epub_') for p in (sd / '.crosspoint').iterdir())
assert '.reader-test { text-align: right; }' in (cache / 'fb2.css').read_text(encoding='utf-8')
assert (cache / 'i0.png').read_bytes() == png
html = '\n'.join((cache / f'c{i}.xhtml').read_text(encoding='utf-8') for i in range(chapter_count))
assert 'fb2-title' in html and 'heading-1' in html and 'heading-2' in html
assert '<strong ' in html and '>жирный</strong>' in html and '<em ' in html and '>курсив</em>' in html
assert 'epub:type="noteref"' in html and 'src="i0.png"' in html
assert 'class="fb2-' in html and 'reader-test' in html and 'style="font-weight: bold"' in html
assert 'href="c3.xhtml#note"' in html, 'direct reader must resolve a forward footnote reference across bodies'
assert 'epub:type="footnote"' in html, 'direct reader must mark the note section'
assert 'data-fb2-xpath="/FictionBook[1]/body[1]/section[1]/section[1]"' in html, \
    'direct reader must preserve exact paths for nested sections'
for filename in ('c0.xhtml', 'c1.xhtml', 'c2.xhtml', 'c3.xhtml'):
    ET.parse(cache / filename)
again = subprocess.run([str(exe), '--reader-cache', '/' + name], env=env, capture_output=True, text=True)
assert again.returncode == 0 and again.stdout == r.stdout, (again.stdout, again.stderr)
print('PASS: direct FB2 reader cache preserves source, formatting, notes and embedded images without EPUB')

# The direct-reader route must reject malformed structures just like the explicit
# publisher path and remove any half-written cache rather than leaving a usable-looking book.
invalid_direct = (
    ('direct-truncated.fb2', valid.encode()[:-20]),
    ('direct-wrong-root.fb2', b'<html><body><p>not an FB2</p></body></html>'),
    ('direct-bad-base64.fb2', valid.replace(base64.b64encode(png).decode(), '@@@').encode()),
    ('direct-duplicate-id.fb2', valid.replace('id="two"', 'id="one"').encode()),
    ('direct-too-deep.fb2', ('<FictionBook><body>'+'<section>'*70+'<p>x</p>'+'</section>'*70+'</body></FictionBook>').encode()),
)
for filename, data in invalid_direct:
    source = sd / filename
    source.write_bytes(data)
    before = hashlib.sha256(data).digest()
    r = subprocess.run([str(exe), '--reader-cache', '/' + filename], env=env, capture_output=True)
    assert r.returncode != 0 and r.stderr, (filename, r.stdout, r.stderr)
    assert hashlib.sha256(source.read_bytes()).digest() == before, filename
    assert not source.with_suffix('.epub').exists() and not source.with_name('.' + source.name).exists(), filename
    assert not list((sd / '.crosspoint' / 'tmp').glob('*.part')), filename
    print('PASS: direct reader rejects malformed input and preserves source:', filename)

# FB2.ZIP/FBZ archives enter the same direct-reader path. The single source is
# extracted inside that book's private tmp cache, never beside the archive or as EPUB.
archive_path = sd / 'direct-reader.fb2.zip'
with zipfile.ZipFile(archive_path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('Books/Direct-Reader.FB2', valid.encode())
archive_before = hashlib.sha256(archive_path.read_bytes()).digest()
r = subprocess.run([str(exe), '--reader-cache', '/direct-reader.fb2.zip'], env=env, capture_output=True, text=True)
assert r.returncode == 0, (r.stdout, r.stderr)
archive_lines = r.stdout.splitlines()
assert archive_lines[0].startswith('/.crosspoint/tmp/fb2-reader-v3-'), archive_lines
archive_cache = sd / archive_lines[0].lstrip('/')
assert (archive_cache / 'archive-source.fb2').read_bytes() == valid.encode()
assert (archive_cache / 'reader.meta').is_file() and (archive_cache / 'c0.xhtml').is_file()
assert hashlib.sha256(archive_path.read_bytes()).digest() == archive_before
assert not (sd / 'direct-reader.epub').exists() and not (sd / 'direct-reader.fb2.epub').exists()
again = subprocess.run([str(exe), '--reader-cache', '/direct-reader.fb2.zip'], env=env, capture_output=True, text=True)
assert again.returncode == 0 and again.stdout == r.stdout, (again.stdout, again.stderr)
print('PASS: deflated FB2 ZIP opens directly, validates CRC, preserves archive and reuses tmp cache')

# Plain .zip is a common distribution name for an archive containing one FB2.
plain_zip_path = sd / 'direct-reader.zip'
with zipfile.ZipFile(plain_zip_path, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('Books/Direct-Reader.FB2', valid.encode())
r = subprocess.run([str(exe), '--reader-cache', '/direct-reader.zip'], env=env, capture_output=True, text=True)
assert r.returncode == 0, (r.stdout, r.stderr)
plain_zip_cache = sd / r.stdout.splitlines()[0].lstrip('/')
assert (plain_zip_cache / 'archive-source.fb2').read_bytes() == valid.encode()
print('PASS: plain .zip containing one FB2 opens through the direct FB2 reader')

# The alternate .fbz suffix follows the same direct-reader route.
fbz_path = sd / 'direct-reader.fbz'
fbz_path.write_bytes(archive_path.read_bytes())
r = subprocess.run([str(exe), '--reader-cache', '/direct-reader.fbz'], env=env, capture_output=True, text=True)
assert r.returncode == 0, (r.stdout, r.stderr)
fbz_cache = sd / r.stdout.splitlines()[0].lstrip('/')
assert (fbz_cache / 'archive-source.fb2').read_bytes() == valid.encode()
print('PASS: .fbz archive opens through the direct FB2 reader')

multi_archive = sd / 'multiple.fb2.zip'
with zipfile.ZipFile(multi_archive, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    archive.writestr('one.fb2', valid.encode())
    archive.writestr('two.fb2', valid.encode())
r = subprocess.run([str(exe), '--reader-cache', '/multiple.fb2.zip'], env=env, capture_output=True)
assert r.returncode != 0 and b'more than one' in r.stderr, (r.stdout, r.stderr)
assert not (sd / 'multiple.epub').exists()
print('PASS: ambiguous FB2 ZIP with multiple books is rejected')

# Corrupt the central-directory CRC while keeping the compressed payload valid.
# The reader must reject and remove the extracted source rather than publishing
# a cache that looks like a successfully parsed book.
bad_crc = sd / 'bad-crc.fb2.zip'
bad_crc.write_bytes(archive_path.read_bytes())
raw = bytearray(bad_crc.read_bytes())
central = raw.find(b'PK\x01\x02')
assert central >= 0
raw[central + 16] ^= 1
bad_crc.write_bytes(raw)
before = hashlib.sha256(bad_crc.read_bytes()).digest()
cache_sources_before = {str(path) for path in (sd / '.crosspoint' / 'tmp').glob('*/archive-source.fb2')}
r = subprocess.run([str(exe), '--reader-cache', '/bad-crc.fb2.zip'], env=env, capture_output=True)
assert r.returncode != 0 and b'CRC validation' in r.stderr, (r.stdout, r.stderr)
assert hashlib.sha256(bad_crc.read_bytes()).digest() == before
cache_sources_after = {str(path) for path in (sd / '.crosspoint' / 'tmp').glob('*/archive-source.fb2')}
assert cache_sources_after == cache_sources_before
print('PASS: FB2 ZIP with a bad central CRC is rejected and extracted source is cleaned')

# Enforce the 128 MiB uncompressed-entry bound from central-directory metadata
# before opening/inflating the payload.
oversize = sd / 'oversize.fb2.zip'
oversize.write_bytes(archive_path.read_bytes())
raw = bytearray(oversize.read_bytes())
local = raw.find(b'PK\x03\x04')
central = raw.find(b'PK\x01\x02')
assert local >= 0 and central >= 0
declared = 128 * 1024 * 1024 + 1
raw[local + 22:local + 26] = declared.to_bytes(4, 'little')
raw[central + 24:central + 28] = declared.to_bytes(4, 'little')
oversize.write_bytes(raw)
r = subprocess.run([str(exe), '--reader-cache', '/oversize.fb2.zip'], env=env, capture_output=True)
assert r.returncode != 0 and b'larger than 128 MB' in r.stderr, (r.stdout, r.stderr)
print('PASS: FB2 ZIP over 128 MiB declared size is rejected before extraction')
