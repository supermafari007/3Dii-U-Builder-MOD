"""Who 3Dii U Builder is built on: what pipeline.py --credits prints and the window's About shows. The full notices and
licence texts are THIRD_PARTY_LICENSES.md, which the app ships beside itself (make_app.py) and the repository keeps at
its root; keep the two lists in step."""

APP, VERSION = '3Dii-U Builder MOD', '1.0.0 MOD'
LICENCE = 'EUPL-1.2'
SOURCE = 'https://github.com/supermafari007/3Dii-U-Builder-MOD'
ORIGINAL = 'https://github.com/J-Glitch2502/3dii-u'
DESIGN = 'syntcity'  # the Builder's UI and art design
CONTRIBUTORS = 'sennecaelen and Clone-34242342'  # code from their pull requests
FORK_NOTICE = f'Fork modificado de 3Dii U Builder, proyecto original: {ORIGINAL}.'

# (what, by whom, licence, where): the projects whose code is in the app or in the games it makes
UPSTREAM = [
    ('3dsrecomp, el recompilador y runtime en el que se basa 3Dii U', 'Nathan Soufflet (nathsou)', 'EUPL-1.2',
     'https://github.com/nathsou/3dsrecomp'),
    ('devkitPPC (GCC, Binutils, newlib) y wut', 'devkitPro and the wut authors', 'GPL-3.0, BSD-style, Zlib',
     'https://devkitpro.org'),
    ('wut-tools (elf2rpl)', 'devkitPro', 'GPL-2.0-or-later', 'https://github.com/devkitPro/wut-tools'),
    ('CafeGLSL, el compilador de shaders de Wii U', 'Exzap, on Mesa', 'MIT', 'https://github.com/Exzap/CafeGLSL'),
    ('Decodificador AAC de FFmpeg', 'the FFmpeg developers', 'LGPL-2.1-or-later', 'https://ffmpeg.org'),
    ('makerom (Project_CTR)', '3DSGuy, applestash, Jakcron', 'MIT', 'https://github.com/3DSGuy/Project_CTR'),
    ('pyctr', 'Ian Burgwin (ihaveamac)', 'MIT', 'https://github.com/ihaveamac/pyctr'),
    ('PyCryptodome y cryptography', 'Helder Eijs; the PyCA contributors', 'BSD-2-Clause; Apache-2.0 or BSD-3-Clause',
     'https://www.pycryptodome.org'),
    ('Qt y PySide6', 'The Qt Company and contributors', 'LGPL-3.0', 'https://www.qt.io'),
    ('Python', 'Python Software Foundation', 'PSF-2.0', 'https://www.python.org'),
    ('Bootloader de PyInstaller', 'PyInstaller Development Team', 'GPL-2.0-or-later with its bootloader exception',
     'https://pyinstaller.org'),
    ('Vídeo de arranque de woomïnstaller', 'shinyquagsire23', 'LGPL-2.1', 'https://github.com/shinyquagsire23/woominstaller'),
]

NOT_AFFILIATED = ('3Dii U no está afiliado, respaldado ni patrocinado por Nintendo. Está diseñado para juegos y archivos del sistema '
                  'extraídos de tus propias consolas y para software homebrew.')


def text():
    """the credits as plain text, for the command line"""
    lines = [f'{APP} {VERSION}, {FORK_NOTICE}', f'Software libre y de código abierto bajo licencia {LICENCE}: {SOURCE}',
             f'Diseño de interfaz y arte: {DESIGN}', f'Contribuciones de código: {CONTRIBUTORS}', '', 'Basado en:']
    lines += [f'  {what}: {who} ({lic}), {url}' for what, who, lic, url in UPSTREAM]
    lines += ['', 'Avisos completos y textos de licencia: THIRD_PARTY_LICENSES.md, junto a la aplicación y en el código fuente.', '', NOT_AFFILIATED]
    return '\n'.join(lines)


def about(tag=''):
    """the credits for the About dialog, short enough to fit it: the projects by name; --credits and the file have the rest"""
    return (f'Versión {VERSION}' + (f' ({tag})' if tag else '') + f'. {FORK_NOTICE}\n\n'
            f'Software libre y de código abierto bajo licencia {LICENCE}: {SOURCE}\n\n'
            f'Diseño de interfaz y arte original por {DESIGN}. Contribuciones de código por {CONTRIBUTORS}.\n\n'
            'Basado en 3dsrecomp de Nathan Soufflet (nathsou), devkitPPC y wut de devkitPro, CafeGLSL de Exzap, FFmpeg, '
            'makerom de 3DSGuy, applestash y Jakcron, pyctr de Ian Burgwin, PyCryptodome, cryptography, Qt y PySide6, '
            'Python, PyInstaller y woomïnstaller de shinyquagsire23.\n\n'
            'Sus avisos y textos completos de licencia se encuentran en THIRD_PARTY_LICENSES.md, junto al programa.\n\n' + NOT_AFFILIATED)
