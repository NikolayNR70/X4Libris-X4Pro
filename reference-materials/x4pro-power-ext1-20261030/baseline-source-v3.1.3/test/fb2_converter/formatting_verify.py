"""Verify semantic formatting using the production converter; optional real FB2.

Usage: python formatting_verify.py converter scratch-dir [book.fb2]
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import xml.etree.ElementTree as ET
import zipfile

exe = Path(sys.argv[1]).resolve()
sd = Path(sys.argv[2]).resolve()
sd.mkdir(parents=True, exist_ok=True)
env = dict(os.environ, FB2_TEST_SD=str(sd))
ns = '{http://www.w3.org/1999/xhtml}'

def convert(name, data):
    (sd / name).write_bytes(data)
    result = subprocess.run([str(exe), '/' + name], env=env, capture_output=True, check=True)
    output, original = result.stdout.decode().splitlines()
    assert output == '/' + str(Path(name).with_suffix('.epub'))
    assert original == output
    assert (sd / ('.' + name)).read_bytes() == data
    return sd / output.lstrip('/')

def inspect(path):
    with zipfile.ZipFile(path) as z:
        assert z.testzip() is None
        css = z.read('fb2.css').decode()
        assert '.fb2-title p' in css and 'hyphens: none' in css
        assert 'font-size: 1.3em' not in css, 'reader heading size controls must own FB2 title sizing'
        manifest = ET.fromstring(z.read('content.opf'))
        assert any(e.get('href') == 'fb2.css' and e.get('media-type') == 'text/css' for e in manifest.iter())
        roots = [ET.fromstring(z.read(n)) for n in z.namelist() if n.endswith('.xhtml')]
        for root in roots:
            assert root.find(ns+'head/'+ns+'link').get('href') == 'fb2.css'
        return roots

def inspect_direct(name, data):
    source = sd / name
    source.write_bytes(data)
    result = subprocess.run([str(exe), '--reader-cache', '/' + name], env=env, capture_output=True, check=True)
    lines = result.stdout.decode().splitlines()
    assert lines[0].startswith('/.crosspoint/tmp/fb2-reader-v3-'), lines
    cache = sd / lines[0].lstrip('/')
    roots = [ET.parse(cache / f'c{i}.xhtml').getroot() for i in range(int(lines[4]))]
    original = ET.fromstring(data)
    bodies = [e for e in original if e.tag.rsplit('}', 1)[-1] == 'body']
    normalize = lambda text: ''.join(text.split())
    assert normalize(''.join(''.join(b.itertext()) for b in bodies)) == \
           normalize(''.join(''.join(r.find(ns+'body').itertext()) for r in roots)), name
    assert not any(p.name.startswith('epub_') for p in (sd / '.crosspoint').iterdir())
    assert not source.with_suffix('.epub').exists() and not source.with_name('.' + source.name).exists()
    return roots, cache

sample = b'''<FictionBook><stylesheet type="text/css">.reader-test { text-align: right; }</stylesheet>
<body><section><title><p>Two</p><p>lines</p></title>
<subtitle>Subtitle</subtitle><p><strong>Standalone section heading</strong></p>
<p class="reader-test" style="font-weight: bold">Body <emphasis>italic</emphasis> <strong>bold</strong>.</p>
<epigraph><p>Epigraph</p><text-author>Author</text-author></epigraph>
<cite><p>Quote</p></cite><poem><stanza><v>Verse one</v><v>Verse two</v></stanza>
<text-author>Poet</text-author></poem><date>Today</date><empty-line/>
<section><title><p>Inner section</p></title><p>Nested body.</p></section>
</section></body></FictionBook>'''
roots = inspect(convert('roles.fb2', sample))
elements = [e for r in roots for e in r.iter()]
for cls, tag in [('fb2-title','div'), ('fb2-subtitle','p'), ('fb2-epigraph','blockquote'),
                 ('fb2-text-author','p'), ('fb2-cite','blockquote'), ('fb2-poem','div'),
                 ('fb2-stanza','div'), ('fb2-verse','p'), ('fb2-date','p'), ('fb2-empty-line','p')]:
    assert any(e.tag == ns+tag and cls in e.get('class','').split() for e in elements), cls
title = next(e for e in elements if 'fb2-title' in e.get('class','').split())
assert len(title.findall(ns+'p')) == 2 and 'style' not in title.attrib
assert 'title' in title.get('class').split(), 'Reader must recognize heading hyphenation policy'
assert 'heading-1' in title.get('class').split(), 'top-level FB2 title must use level-1 reader controls'
assert any('fb2-title' in e.get('class','').split() and 'heading-2' in e.get('class','').split()
           for e in elements), 'nested FB2 section titles must use level-2 reader controls'
assert any('fb2-heading-2' in e.get('class','').split() and 'heading-2' in e.get('class','').split()
           and ''.join(e.itertext()).strip() == 'Standalone section heading' for e in elements), \
       'strong-only leading FB2 paragraphs must be level-2 headings'
assert not any('fb2-heading-2' in e.get('class','').split() and 'Body' in ''.join(e.itertext()) for e in elements), \
       'ordinary body paragraphs must not inherit heading alignment'
assert sum(e.tag == ns+'em' for e in elements) == 1
assert sum(e.tag == ns+'strong' for e in elements) == 2
print('PASS: title roles, multiline headings, citation/poetry roles, stylesheet, inline emphasis')
direct_roots, direct_cache = inspect_direct('direct-roles.fb2', sample)
direct_elements = [e for r in direct_roots for e in r.iter()]
assert any(e.get('class', '').endswith('reader-test') and e.get('style') == 'font-weight: bold'
           for e in direct_elements), 'direct reader must retain FB2 class/style attributes'
assert '.reader-test { text-align: right; }' in (direct_cache/'fb2.css').read_text(encoding='utf-8')
print('PASS: direct FB2 path retains body text, style sheet, class/style and creates no EPUB cache')

# Move a complete current-format cache back to the former root layout to model a v2/v3-dev SD card.
# Opening the same source must migrate the directory intact instead of rebuilding or losing state.
legacy_cache = sd / '.crosspoint' / direct_cache.name
legacy_cache.parent.mkdir(parents=True, exist_ok=True)
sentinel = direct_cache / 'migration-sentinel.txt'
sentinel.write_text('keep-progress-and-bookmarks', encoding='ascii')
direct_cache.rename(legacy_cache)
direct_roots, direct_cache = inspect_direct('direct-roles.fb2', sample)
assert direct_cache.exists() and not legacy_cache.exists(), 'legacy FB2 cache was not migrated'
assert (direct_cache/'migration-sentinel.txt').read_text(encoding='ascii') == 'keep-progress-and-bookmarks', \
    'legacy FB2 cache contents were rebuilt or lost during migration'
print('PASS: legacy root-level FB2 cache migrates into the shared tmp folder intact')

for label, encoding in (('utf16le', 'utf-16-le'), ('utf16be', 'utf-16-be')):
    inspect_direct(label + '.fb2', ('\ufeff' + sample.decode('utf-8')).encode(encoding))
print('PASS: direct FB2 path decodes UTF-16 little-endian and big-endian')

for version in ('v1', 'v2'):
    legacy = sd / '.crosspoint' / ('fb2-' + version + '-old')
    legacy.mkdir(parents=True, exist_ok=True)
    (legacy/'source.txt').write_text('/Books/original.fb2')
    mapped = subprocess.check_output([str(exe), '--source-path',
                                     '/.crosspoint/'+legacy.name+'/book.epub'], env=env)
    assert mapped == b'/Books/original.fb2'
print('PASS: original source mapping remains readable for both cache versions')

if len(sys.argv) > 3:
    source = Path(sys.argv[3])
    data = source.read_bytes()
    direct_roots, direct_cache = inspect_direct('direct-real-book.fb2', data)
    print(f'PASS: direct FB2 real book: {len(direct_roots)} chapters, full body text/order, no EPUB cache')
    output = convert('real-book.fb2', data)
    roots = inspect(output)
    fb2 = ET.fromstring(data)
    local = lambda e: e.tag.rsplit('}',1)[-1]
    bodies = [e for e in fb2 if local(e) == 'body']
    # Ignore XML indentation but preserve every non-whitespace character in order.
    normalize = lambda s: ''.join(s.split())
    original = normalize(''.join(''.join(b.itertext()) for b in bodies))
    converted = normalize(''.join(''.join(r.find(ns+'body').itertext()) for r in roots))
    assert original == converted, 'Body text/order changed'
    titles = sum(local(e) == 'title' for b in bodies for e in b.iter())
    converted_titles = sum('fb2-title' in e.get('class','').split() for r in roots for e in r.iter())
    assert titles == converted_titles
    if source.name == '01.ПереКРЕСТок одиночества.fb2':
        # This supplied book stores its visible chapter subtitles as bold-only
        # paragraphs after <title>, not as semantic FB2 <title> elements.
        first_section = next(e for b in bodies for e in b.iter() if local(e) == 'section')
        first_children = list(first_section)
        subtitle_lines = 0
        for child in first_children:
            if local(child) == 'p' and len(child) == 1 and local(child[0]) == 'strong':
                subtitle_lines += 1
            elif subtitle_lines and local(child) == 'p':
                break
        generated = sum('fb2-heading-2' in e.get('class','').split() for r in roots for e in r.iter())
        assert generated >= subtitle_lines >= 1, f'Expected centered FB2 subtitles, got {generated}/{subtitle_lines}'
    shutil.copyfile(output, sd/'converted-book.epub')
    print(f'PASS: real book: {len(roots)} chapters, {titles} titles, complete body text/order, original unchanged')
