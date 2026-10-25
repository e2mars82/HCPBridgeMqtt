from pathlib import Path
import zlib

def generate(project):
    web = project / 'WebUI' / 'webpage'
    menu = (web / 'menu.html').read_text(encoding='utf-8-sig')
    for name in ['index', 'flows']:
        source = (web / (name + '.html')).read_text(encoding='utf-8-sig')
        # Explicit markers keep the editable menu and both pages in sync.
        a, b = '<!-- SHARED_MENU_START -->', '<!-- SHARED_MENU_END -->'
        if a in source and b in source:
            before, rest = source.split(a, 1)
            _, after = rest.split(b, 1)
            source = before + a + menu + b + after
            (web / (name + '.html')).write_text(source, encoding='utf-8')
        write_asset(project / 'WebUI' / (name + '_html.h'), name + '_html', source)
    assets = [('diagnostics_js', web / 'diagnostics.js'), ('i18n_js', web / 'i18n.js'), ('language_de', web / 'i18n/de.json'), ('language_en', web / 'i18n/en.json')]
    header = ''
    for name, path in assets:
        header += declaration(name, path.read_text(encoding='utf-8-sig')) + '\n'
    (project / 'WebUI/i18n_assets.h').write_text(header, encoding='utf-8')

def declaration(name, text):
    return 'const uint8_t ' + name + '[] PROGMEM = {' + ','.join('0x%02X' % c for c in zlib.compress(text.encode('utf-8'), 9)) + '};'

def write_asset(path, name, text):
    path.write_text(declaration(name, text), encoding='utf-8')

if __name__ == '__main__':
    generate(Path(__file__).resolve().parent.parent)
