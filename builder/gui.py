"""The builder's window. ui/Main.qml draws it; App holds what it shows and does what its buttons ask, running pipeline.py for the build.

Questions come up inside the window one at a time: a choice that asks several things is a generator that yields each question and is sent the
answer (App.run_flow), and messages wait their turn in the same queue."""
import base64
import json
import os
import queue
import shutil
import subprocess
import sys
import tempfile
import threading

import shiboken6
from PySide6.QtCore import QBuffer, QIODevice, QObject, Property, QSize, QStandardPaths, Qt, QTimer, QUrl, Signal, Slot
from PySide6.QtGui import QColor, QFontDatabase, QGuiApplication, QIcon, QImage, QPixmap
from PySide6.QtQml import QQmlApplicationEngine
from PySide6.QtQuick import QQuickWindow
from PySide6.QtWidgets import QApplication, QFileDialog, QInputDialog

import covers
import credits
import decrypt
import dumps
import mods
import pipeline
import saves
import updater
import user_files

APP, VERSION = credits.APP, credits.VERSION
SETTINGS_DIR = '3Dii U Builder'
_HERE_RAW = os.path.dirname(os.path.abspath(__file__))
HERE = os.path.dirname(_HERE_RAW) if _HERE_RAW.lower().endswith('.zip') else _HERE_RAW
FROZEN = getattr(sys, 'frozen', False)
WIN = sys.platform == 'win32'
NO_WINDOW = 0x08000000 if WIN else 0  # the build's compilers would each open a console window otherwise
DUMPS = 'Juegos de 3DS (*.3ds *.cci *.cia *.cxi *.app);;Todos los archivos (*.*)'
DUMP_EXTS = ('.3ds', '.cci', '.cia', '.cxi', '.app')
STAGE_OF = {n: i for i, (n, _) in enumerate(pipeline.STAGES)}
NO_ART = "Icono del juego"
NO_ICON_ART = 'Igual que la portada'  # the Wii U Menu icon without a picture of its own
NO_BEZEL = 'Fondo negro'  # the GamePad beside the screens without a picture
NO_PAD_ART = 'Igual que en TV'  # the GamePad's start-up screen without a picture of its own
NO_SAVE = 'Ninguna (crear partida nueva)'  # no 3DS save packed in
NO_GAME_SOUND = 'Sonido general'  # a game without a start-up sound of its own: the one for every game, if there is one
BOOT_SOUND = os.path.join(os.path.dirname(pipeline.ARCHIVES), 'boot_sound.btsnd')  # where every build picks it up (tools/make_game_title.py)
GAME_SOUNDS = os.path.join(os.path.dirname(BOOT_SOUND), 'boot_sounds')  # a game's own, <title id>.btsnd
LIBRARY_MAX = 40  # games the Games page remembers
# What each of "Your Files" is and how to get it from your own consoles: the Info buttons, and the message when one is missing.
# Testers asked what the system files were and how to get seeddb.bin (Nov 9 and 10).
HELP = {
    'keys': ('Claves de 3DS',
             'Dos archivos de tu consola 3DS necesarios para abrir volcados cifrados: boot9.bin y seeddb.bin. Los volcados ya descifrados no los requieren.\n\n'
             'Ambos se obtienen con GodMode9 en una 3DS con custom firmware (boot9strap):\n'
             '•  boot9.bin: entra en [M:] MEMORY VIRTUAL, selecciona boot9.bin y presiona Y. Luego abre [0:] SDCARD, presiona Y de nuevo y elige Copy.\n'
             '•  seeddb.bin: presiona HOME, selecciona More... y después Build support files. Se guardará en gm9/out dentro de la tarjeta SD.\n\n'
             'En tu PC, coloca ambos archivos en una misma carpeta (por defecto, .3ds dentro de tu carpeta de usuario) o presiona «Carpeta» para seleccionarla.'),
    'otp': ('otp.bin de Wii U',
            'Un archivo de exactamente 1024 bytes extraído de tu consola Wii U. El juego se empaqueta con la clave común de este archivo para que tu consola pueda instalarlo. El programa nunca muestra ni almacena esa clave.\n\n'
            'En una Wii U con Aroma, ejecuta Wii U NAND Dumper, elige Dump OTP y se guardará otp.bin en la tarjeta SD. Si ya hiciste una copia de seguridad de la NAND, otp.bin estará incluido en ella.\n\n'
            'En tu PC, presiona «Archivo» y selecciónalo.'),
    'system': ('Archivos del sistema de 3DS',
               'Archivos internos de la consola 3DS requeridos por los juegos: la fuente compartida del sistema, los recursos gráficos para dibujar Miis y la lista de palabras del sistema. No vienen dentro de los volcados de juegos, por lo que el programa los integra automáticamente en cada compilación.\n\n'
               'Puedes obtenerlos de tu 3DS de cualquiera de estas dos formas:\n'
               '•  Con el emulador Azahar: usa File > Set Up System Files conectando Artic Setup Tool desde tu 3DS. El programa los detectará automáticamente.\n'
               '•  Con GodMode9 en tu 3DS: entra en [1:] SYSNAND CTRNAND > title y copia las carpetas 0004009b (fuente y datos de Mii) y 000400db (lista de palabras) a tu tarjeta SD usando Y > Copy. Luego, en tu PC, presiona «Carpeta» aquí y selecciona la carpeta 0004009b.\n\n'
               'Solo necesitas configurarlos una vez; se conservarán para todas las compilaciones futuras.'),
}


# ---- settings, the PC's drives, and words for what was chosen
def settings_file():
    base = os.environ.get('APPDATA') if WIN else os.path.join(os.path.expanduser('~'), '.config')
    return os.path.join(base or HERE, SETTINGS_DIR, 'settings.json')


def covers_folder():
    return os.path.join(os.path.dirname(settings_file()), 'covers')


FROM_SGDB, FROM_TDB = 'De SteamGridDB', 'De GameTDB'


def picked(path):
    """whether a picture is one the player chose, not one fetched into the covers folder (covers.py)"""
    return bool(path) and os.path.dirname(os.path.abspath(path)) != os.path.abspath(covers_folder())


def source(path):
    """a picture's line in the window: where it was fetched from, or its file's name"""
    if picked(path):
        return os.path.basename(path)
    return FROM_SGDB if os.path.basename(path).startswith('sgdb_') else FROM_TDB


def load_settings():
    try:
        return json.load(open(settings_file(), encoding='utf-8'))
    except (OSError, ValueError):
        return {}


def save_settings(s):
    os.makedirs(os.path.dirname(settings_file()), exist_ok=True)
    json.dump(s, open(settings_file(), 'w', encoding='utf-8'), indent=1)


def emulator_folders(everywhere=True):
    """Folders with a 3DS NAND, where the system archives (shared font, Mii data) can come from: Azahar's, Citra's and Lime3DS's user folders,
    then (everywhere) any folder a few levels down in Downloads, Desktop and Documents, whatever its name: a portable emulator's user folder,
    or a copy of one. A Miitopia built without them stopped on the console, the files being in such a copy in Downloads (Nov 9)."""
    home = os.path.expanduser('~')
    base = os.environ.get('APPDATA', '') if WIN else os.path.join(home, '.local', 'share')
    names = ('Azahar', 'Citra', 'Lime3DS') if WIN else ('azahar-emu', 'citra-emu', 'lime3ds-emu')
    found = [p for p in (os.path.join(base, n) for n in names) if dumps.nand_titles(p)]
    if not everywhere:
        return found
    places = (QStandardPaths.StandardLocation.DownloadLocation, QStandardPaths.StandardLocation.DesktopLocation,
              QStandardPaths.StandardLocation.DocumentsLocation)
    tops = [QStandardPaths.writableLocation(p) for p in places]  # where Windows really keeps them (OneDrive, moved), then where they usually are
    tops += [os.path.join(home, d) for d in ('Downloads', 'Desktop', 'Documents')]
    seen = set()
    for top in tops:
        if top and os.path.normcase(os.path.abspath(top)) not in seen:
            seen.add(os.path.normcase(os.path.abspath(top)))
            found += nands_below(top, 3)  # deeper costs seconds on a big Downloads folder, and these sit near the top
    return found


def nands_below(top, depth):
    """the folders holding a 3DS NAND up to depth levels below top; one that holds a NAND isn't searched further"""
    found, level = [], [top]
    for _ in range(depth):
        below = []
        for folder in level:
            try:
                subs = [e.path for e in os.scandir(folder) if e.is_dir(follow_symlinks=False)]
            except OSError:
                continue
            for sub in subs:
                if dumps.nand_titles(sub):
                    found.append(sub)
                else:
                    below.append(sub)
        level = below
    return found


def sd_cards():
    """removable drives, the ones already set up for a Wii U first"""
    found = []
    if WIN:
        import ctypes
        mask = ctypes.windll.kernel32.GetLogicalDrives()
        for i in range(26):
            root = f'{chr(65 + i)}:\\'
            if mask >> i & 1 and ctypes.windll.kernel32.GetDriveTypeW(root) == 2:  # DRIVE_REMOVABLE
                found.append(root)
    elif sys.platform == 'darwin':
        if os.path.isdir('/Volumes'):
            for d in sorted(os.listdir('/Volumes')):
                p = os.path.join('/Volumes', d)
                if os.path.isdir(p) and not os.path.islink(p) and d not in ('Macintosh HD', 'Recovery', 'Preboot', 'VM', 'Update'):
                    found.append(p)
    else:
        user = os.environ.get('USER', '')
        for base in (f'/media/{user}', f'/run/media/{user}'):
            if os.path.isdir(base):
                found += [os.path.join(base, d) for d in sorted(os.listdir(base))]
    return sorted(found, key=lambda d: not os.path.isdir(os.path.join(d, 'wiiu')))


def describe(d):
    if d is None:
        return ''
    v = f'v{d.version >> 10}.{d.version >> 4 & 63}.{d.version & 15}'
    return f'{v}, {d.size >> 20} MB' if d.kind == 'update' else f'{len([c for c in d.cia.contents if c[4] is not None])} contenidos, {d.size >> 20} MB' \
        if d.cia else f'{d.size >> 20} MB'


def otp_ok(path):
    return user_files.check_otp(path) is None


def short(path, n=46, end=False):
    """a path cut to n characters from the middle (or the end), for labels"""
    if len(path) <= n:
        return path
    return path[:n - 3] + '...' if end else path[:n // 2 - 1] + '...' + path[-(n // 2 - 2):]


# ---- pictures
def png_url(img):
    buf = QBuffer()
    buf.open(QIODevice.WriteOnly)
    img.save(buf, 'PNG')
    return 'data:image/png;base64,' + base64.b64encode(bytes(buf.data())).decode()


def icon_url(rows):
    """a game's 48x48 icon (rows of RGB) as a picture the window can show"""
    img = QImage(48, 48, QImage.Format_RGB32)
    for y, row in enumerate(rows):
        for x, (r, g, b) in enumerate(row):
            img.setPixel(x, y, QColor(r, g, b).rgb())
    return png_url(img)


def draw_icon(size):
    """the app's icon, the 3Dii U logo (icon.png, the user's, 2026-11-09), at size x size: the window's, the rail's and the exe's (make_app.py)"""
    img = QImage(os.path.join(HERE, 'icon.png'))
    return img.scaled(size, size, Qt.KeepAspectRatio, Qt.SmoothTransformation).convertToFormat(QImage.Format_ARGB32)


def theme():
    families = set(QFontDatabase.families())
    body = next((f for f in ('Segoe UI Variable Text', 'Segoe UI', 'Cantarell', 'Noto Sans') if f in families), '')
    display = next((f for f in ('Segoe UI Variable Display', 'Segoe UI', 'Cantarell') if f in families), body)
    # the Wii U Menu's glass: grey cards with a curved gloss, grey text, cyan for chosen and red for missing; dark is the same glass inverted
    light = {'pageTop': '#ffffff', 'pageBottom': '#eef1f0', 'card': '#cbd2d0', 'gloss': '#dde1e0', 'shadow': '#40000000',
             'title': '#7e8b88', 'sub': '#a8b3b0', 'label': '#6c7976', 'subLabel': '#a3aeab',
             'cyanBase': '#9fdde8', 'cyanText': '#35b5cb', 'redBase': '#eba3aa', 'redText': '#c93a4c', 'greyBase': '#bfc7c5', 'greyText': '#6c7976',
             'lightRed': '#c9283c', 'lightCyan': '#2fb4cc', 'lightGreen': '#3aa856', 'lightGrey': '#aab4b1', 'socket': '#98a3a0',
             'tileGloss': '#ffffff', 'tileBase': '#cdd3d1', 'plus': '#8b9996', 'buildText': '#3c4846',
             'rail': '#e6e9e8', 'railTop': '#dde1e0', 'railActive': '#f3f5f4', 'railLine': '#fbfcfc', 'railEdge': '#cfd5d3', 'railText': '#7b8885',
             'help': '#5c6a67', 'helpActive': '#6e7d7a', 'helpText': '#ffffff', 'helpRing': '#3c4846', 'helpFace': '#ffffff',
             'keycap': '#ffffff', 'keycapBase': '#c3cac8', 'keycapSide': '#a3aeab', 'keycapGlyph': '#a9b4b1', 'tileRim': '#e4e8e7', 'tileEdge': '#b9c3c0',
             'red': '#d6394b', 'cyan': '#3dbdd6', 'builder': '#7b8885', 'ds': '#e5959f', 'screen': '#ffffff', 'dotFrom': '#e8a4ad', 'dotMid': '#c9c3d8',
             'dotTo': '#a9dde7', 'pad': '#8fdbe7', 'dim': '#73000000', 'logBg': '#e9eceb', 'logText': '#4a5654'}
    dark = dict(light, pageTop='#20272a', pageBottom='#1a2023', card='#313b3e', gloss='#3b4649', shadow='#80000000',
                title='#d7dedc', sub='#8f9c99', label='#c5cecb', subLabel='#8b9895',
                cyanBase='#1f6f80', cyanText='#9fe6f2', redBase='#7a2a34', redText='#ffb3bb', greyBase='#4a5558', greyText='#d0d7d5',
                lightGrey='#5f6c6f', socket='#1d2427', tileGloss='#5f6c6f', tileBase='#2e383b', plus='#5a6769', buildText='#d7dedc',
                rail='#2a3336', railTop='#252d30', railActive='#344043', railLine='#1d2427', railEdge='#1d2427', railText='#c5cecb',
                help='#11181a', helpActive='#1f2a2d', helpRing='#000000', helpFace='#d7dedc',
                keycap='#5a6669', keycapBase='#3a4548', keycapSide='#263033', keycapGlyph='#8f9c99', tileRim='#45524f', tileEdge='#252e31', builder='#c9d1cf', screen='#2a3336',
                dim='#a6000000', logBg='#1d2427', logText='#c5cecb')
    return {'font': body, 'display': display, 'mono': 'Cascadia Mono' if 'Cascadia Mono' in families else 'Consolas', 'light': light, 'dark': dark}


# ---- what the window shows, and what its buttons do
class App(QObject):
    changed = Signal()
    coverReady = Signal(str, str, str)  # title id, the picture's path, the icon's: from fetch_cover's thread
    releaseReady = Signal(str, str)  # title id and GameTDB release line: from fetch_cover's thread
    layoutWanted = Signal(str)  # setLayout: run() swaps the window
    keyRefused = Signal()  # SteamGridDB turned the key down: from fetch_cover's thread
    updateEvent = Signal(str, object)  # what, value: from the updater's worker threads, the value passed as it is
    logChanged = Signal()

    def _p(kind, name, notify=changed):
        return Property(kind, lambda self: self._s[name], notify=notify)

    hasGame, gameName, maker, badge, known = _p(bool, 'hasGame'), _p(str, 'gameName'), _p(str, 'maker'), _p(str, 'badge'), _p(bool, 'known')
    note, meta, icon = _p(str, 'note'), _p(str, 'meta'), _p(str, 'icon')
    releaseText = _p(str, 'releaseText')
    updateText, hasUpdate, dlcText, hasDlc = _p(str, 'updateText'), _p(bool, 'hasUpdate'), _p(str, 'dlcText'), _p(bool, 'hasDlc')
    modText, hasMod = _p(str, 'modText'), _p(bool, 'hasMod')
    artText, hasArt, artUrl = _p(str, 'artText'), _p(bool, 'hasArt'), _p(str, 'artUrl')
    iconText, hasIconArt, iconArtUrl, tileUrl = _p(str, 'iconText'), _p(bool, 'hasIconArt'), _p(str, 'iconArtUrl'), _p(str, 'tileUrl')
    bezelText, hasBezel = _p(str, 'bezelText'), _p(bool, 'hasBezel')
    padArtText, hasPadArt, padArtUrl, gameSoundText, hasGameSound = _p(str, 'padArtText'), _p(bool, 'hasPadArt'), _p(str, 'padArtUrl'), _p(str, 'gameSoundText'), _p(bool, 'hasGameSound')
    saveText, hasSave = _p(str, 'saveText'), _p(bool, 'hasSave')
    isMk7, mk7Online = _p(bool, 'isMk7'), _p(bool, 'mk7Online')
    artKeySet, soundSet, soundCustom, artStyle = _p(bool, 'artKeySet'), _p(bool, 'soundSet'), _p(bool, 'soundCustom'), _p(str, 'artStyle')
    library = _p('QVariantList', 'library')
    templateUrl = _p(str, 'templateUrl')
    logoUrl = _p(str, 'logoUrl')
    keysOk, keysNeeded, keysText, otpOk, otpText = _p(bool, 'keysOk'), _p(bool, 'keysNeeded'), _p(str, 'keysText'), _p(bool, 'otpOk'), _p(str, 'otpText')
    sysOk, sysText = _p(bool, 'sysOk'), _p(str, 'sysText')
    target, destText, cards = _p(str, 'target'), _p(str, 'destText'), _p('QVariantList', 'cards')
    building, view = _p(bool, 'building'), _p(str, 'view')
    stages, stage, failed, progress, detail = _p('QVariantList', 'stages'), _p(int, 'stage'), _p(bool, 'failed'), _p(float, 'progress'), _p(str, 'detail')
    log = _p(str, 'log', logChanged)
    resultText, where, dialog = _p(str, 'resultText'), _p(str, 'where'), _p('QVariant', 'dialog')
    games, others, version, rootOk = _p('QVariantList', 'games'), _p('QVariantList', 'others'), _p(str, 'version'), _p(bool, 'rootOk')
    faq, themeMode, dark = _p('QVariantList', 'faq'), _p(str, 'themeMode'), _p(bool, 'dark')
    updateState, updateTitle, updateNotes, updateProgress = _p(str, 'updateState'), _p(str, 'updateTitle'), _p(str, 'updateNotes'), _p(int, 'updateProgress')
    updateMessage, releaseTag, updateSetAside = _p(str, 'updateMessage'), _p(str, 'releaseTag'), _p(bool, 'updateSetAside')
    del _p

    def __init__(self):
        super().__init__()
        self.st = load_settings()
        if not self.st.get('keys'):
            self.st['keys'] = decrypt.KEYS_DEFAULT
        decrypt.use_keys(self.st['keys'])
        self.game = self.update = self.dlc = self.mod = self.art = self.icon_art = None  # the mod, the banner art and the icon art are paths
        self.bezel = None  # the GamePad's picture beside the screens, a path
        self.pad_art = self.game_sound = None  # the GamePad's start-up picture and the game's start-up sound (a .btsnd), paths
        self.save_in = None  # the folder of a 3DS save to pack into the build (saves.py)
        self.refused_key = None  # the SteamGridDB key it turned down, if it did (fetch_cover)
        self.proc, self.result, self.cancelled = None, None, False
        self.lines, self.log_lines, self.dialogs, self.flow = queue.Queue(), [], [], None
        compat = json.load(open(os.path.join(HERE, 'compat.json'), encoding='utf-8'))
        try:  # the Help page's questions and answers: without them the app still runs
            with open(os.path.join(HERE, 'faq.json'), encoding='utf-8') as f:
                faq = json.load(f)
        except (OSError, ValueError):
            faq = []
        faq = [e for e in faq if isinstance(e, dict) and isinstance(e.get('q'), str) and isinstance(e.get('a'), str) and e['q'] and e['a']]             if isinstance(faq, list) else []
        theme_mode = self.st.get('theme')
        self._s = dict(hasGame=False, gameName='', maker='', badge='', known=False, icon='', meta='',
                       note='Selecciona un juego de 3DS para comenzar. El programa leerá su título e icono y te permitirá agregar actualizaciones, DLC o mods.', releaseText='',
                       updateText='Ninguno', hasUpdate=False, dlcText='Ninguno', hasDlc=False, modText='Ninguno', hasMod=False, artText=NO_ART, hasArt=False, artUrl='', iconText=NO_ART, hasIconArt=False, iconArtUrl='', tileUrl='', bezelText=NO_BEZEL, hasBezel=False, padArtText=NO_PAD_ART, hasPadArt=False, padArtUrl='', templateUrl=QUrl.fromLocalFile(os.path.join(pipeline.ROOT, 'phase0', 'title_code', 'meta', 'nintendo_3ds_template.png')).toString(), gameSoundText=NO_GAME_SOUND, hasGameSound=False, saveText=NO_SAVE, hasSave=False, isMk7=False, mk7Online=bool(self.st.get('mk7_pretendo_online', False)), artKeySet=bool(self.st.get('sgdb_key')), soundSet=os.path.isfile(BOOT_SOUND) or os.path.isfile(os.path.join(pipeline.ROOT, 'phase0', 'title_code', 'meta', 'bootSound.btsnd')), soundCustom=os.path.isfile(BOOT_SOUND), artStyle=self.st.get('art_style', 'console'), library=[], keysOk=False, keysNeeded=False, keysText='', otpOk=False,
                       otpText='', sysOk=False, sysText='', target=self.st.get('target', 'card'), destText='', cards=[], building=False, view='form', stages=[],
                       stage=-1, failed=False, progress=0.0, detail='', log='', resultText='', where='', dialog=None, version=VERSION,
                       rootOk=bool(pipeline.ROOT) and os.path.isfile(os.path.join(pipeline.ROOT, 'tools', 'win', 'build3ds.py')),
                       games=[dict(name=g['name'], status=g['status'], note=g['note']) for k, g in compat.items() if not k.startswith('_') and g.get('status') in ('Plays', 'Complete')],
                       others=compat.get('_cemu', []),  # shown only: they build as untested games, by their own code
                       faq=faq, themeMode=theme_mode if theme_mode in ('system', 'light', 'dark') else 'system', dark=False,
                       updateState='', updateTitle='', updateNotes='', updateProgress=0, updateMessage='', releaseTag=updater.own_tag(),
                       updateSetAside=False)
        self._s['templateUrl'] = QUrl.fromLocalFile(os.path.join(pipeline.ROOT, 'phase0', 'title_code', 'meta', 'nintendo_3ds_template_clean.png')).toString()
        self._s['logoUrl'] = QUrl.fromLocalFile(os.path.join(pipeline.ROOT, 'phase0', 'title_code', 'meta', 'nintendo_3ds_logo.png')).toString()
        self.release, self.update_job, self.check_job, self.update_stop, self.shown, self.ask_again = None, None, None, threading.Event(), False, False
        self.set(updateSetAside=self.set_aside(updater.failed_tags()))
        ready = updater.activated()  # a version downloaded before (Later, or a start with Shift held) is ready without a check
        if updater.tag_key(self._s['releaseTag']) and updater.tag_key(ready) and updater.tag_key(ready) > updater.tag_key(self._s['releaseTag']):
            self.set(updateState='ready', updateTitle=ready)
        self.updateEvent.connect(self.update_event)
        self.auto_check = QTimer(self, singleShot=True, interval=4000)  # a few seconds after the window shows: the start isn't slowed
        self.auto_check.timeout.connect(lambda: self.check_update(False))
        QGuiApplication.styleHints().colorSchemeChanged.connect(self.apply_theme)  # "system" follows Windows switching while the window is open
        self.apply_theme()
        self.coverReady.connect(self.cover_ready)
        self.releaseReady.connect(self.release_ready)
        self.keyRefused.connect(self.key_refused)
        self.refresh_library()
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll)
        self.refresh_cards()
        self.refresh_files()

    def set(self, **kw):
        self._s.update(kw)
        self.changed.emit()

    # ---- questions and messages, in the window
    def run_flow(self, gen):
        self.flow = gen
        self.step(None)

    def step(self, value):
        try:
            q = self.flow.send(value)
        except StopIteration:
            self.flow = None
            return
        self.dialogs.append(dict(q, flow=True))
        self.show_dialog()

    def tell(self, title, message, tone='info'):
        self.dialogs.append(dict(title=title, message=message, yes='OK', no='', tone=tone, flow=False))
        self.show_dialog()

    def show_dialog(self):
        head = self.dialogs[0] if self.dialogs else None
        self.set(dialog={k: v for k, v in head.items() if k != 'flow'} if head else None)

    @Slot(bool)
    def answer(self, yes):
        if not self.dialogs:
            return
        head = self.dialogs.pop(0)
        self.show_dialog()
        if head['flow'] and self.flow:
            self.step(yes)

    @Slot()
    def about(self):
        self.tell(f'Acerca de {APP}', credits.about(self._s['releaseTag']))

    @Slot(str)
    def setLayout(self, name):
        """the modern window (ui/Main.qml) or the simple one (ui/Simple.qml): kept in the settings, and the window swapped at once (run)"""
        if name not in ('modern', 'simple') or self._s['building']:
            return
        self.st['layout'] = name
        save_settings(self.st)
        self.layoutWanted.emit(name)

    @Slot(str)
    def setArtStyle(self, style):
        """the start-up screens: the art as it is, or laid out as the Wii U's Virtual Console shows a game (tools/make_game_title.py, console_screens)"""
        if style not in ('picture', 'console'):
            return
        self.st['art_style'] = style
        save_settings(self.st)
        self.set(artStyle=style)

    @Slot(bool)
    def setMk7Online(self, enabled):
        self.st['mk7_pretendo_online'] = bool(enabled)
        save_settings(self.st)
        self.set(mk7Online=bool(enabled))

    @Slot(str)
    def setThemeMode(self, mode):
        if mode not in ('system', 'light', 'dark'):
            return
        self.st['theme'] = mode
        save_settings(self.st)
        self.set(themeMode=mode)
        self.apply_theme()

    def apply_theme(self, *_):
        """asks Qt for the chosen scheme, so the native title bar follows it too; Unknown hands "system" back to Windows"""
        hints, mode = QGuiApplication.styleHints(), self._s['themeMode']
        asked = {'dark': Qt.ColorScheme.Dark, 'light': Qt.ColorScheme.Light}.get(mode, Qt.ColorScheme.Unknown)
        if getattr(self, 'asked', None) != asked:  # setColorScheme signals colorSchemeChanged, which calls this again
            self.asked = asked
            hints.setColorScheme(asked)
        # the mode, not colorScheme() alone: a platform without schemes (offscreen) ignores the request
        self.set(dark=mode == 'dark' or mode == 'system' and hints.colorScheme() == Qt.ColorScheme.Dark)

    # ---- choosing
    def open_dump(self, path):
        try:
            return dumps.Dump(path)
        except Exception as e:  # a truncated or foreign file can fail anywhere in its headers
            self.tell('No se puede leer el archivo', f'{os.path.basename(path)}: {e if isinstance(e, (dumps.DumpError, OSError)) else "no es un volcado de 3DS compatible"}',
                      'error')
            return None

    @Slot()
    def chooseGame(self):
        if self._s['building']:
            return
        path = QFileDialog.getOpenFileName(None, 'Selecciona un juego de 3DS', self.st.get('last_dir', ''), DUMPS)[0]
        if path:
            self.open_game(path)

    def open_game(self, path, ask=True):
        self.st['last_dir'] = os.path.dirname(path)
        d = self.open_dump(path)
        if d is None:
            return
        if d.kind in ('update', 'dlc'):
            self.tell(f'Eso es {"una actualización" if d.kind == "update" else "DLC"}', 'Primero selecciona el juego. Después se te preguntará por su actualización y DLC.')
            return
        if d.kind not in ('game', 'demo'):
            self.tell('Eso no es un juego', 'Parece ser un título del sistema.', 'error')
            return
        if d.encrypted() and not decrypt.have_keys(self.st['keys']):
            self.tell('Este volcado está cifrado', 'Builder necesita las claves de tu propia 3DS para leerlo. Selecciona la carpeta que contiene boot9.bin y seeddb.bin '
                                                'en Claves de 3DS y vuelve a seleccionar el juego.', 'warn')
            return
        try:
            decrypt.use_keys(self.st['keys'])
            d.read_names()
        except dumps.DumpError as e:
            self.tell('No se pudo leer este juego', str(e), 'error')
            return
        self.game, self.update, self.dlc, self.mod = d, None, None, None
        info = pipeline.profile_for(d) or pipeline.derived(d)
        known = info['status'] in ('Plays', 'Complete')  # compat.json: tested on a real Wii U, or finished there
        overrides = self.st.get('metadata', {}).get(f'{d.title_id:016x}', {})
        shown_name = overrides.get('name', info['name'])
        shown_publisher = overrides.get('publisher', d.publisher or '')
        info['name'] = shown_name
        info['publisher'] = shown_publisher
        self.set(hasGame=True, isMk7=(d.title_id in pipeline.MK7_TITLE_IDS), gameName=shown_name, maker=shown_publisher, releaseText='', known=known, badge=('Completo en Wii U' if info['status'] == 'Complete' else 'Jugable en Wii U') if known else 'Sin probar',
                 icon=icon_url(d.icon) if d.icon else '', meta=f'{d.product}  ·  {d.title_id:016X}  ·  {describe(d)}', view='form')
        self.set_extra('update', None)
        self.set_extra('dlc', None)
        self.set_extra('mod', None)
        saved = self.st.get('art', {}).get(f'{d.title_id:016x}')  # a game keeps the banner art chosen for it; '' is "none, on purpose"
        icon = self.st.get('icon_art', {}).get(f'{d.title_id:016x}')
        self.set_art(saved if saved and os.path.isfile(saved) else None, icon if icon and os.path.isfile(icon) else None, remember=False)
        bezel = self.st.get('pad_bezel', {}).get(f'{d.title_id:016x}')
        self.set_bezel(bezel if bezel and os.path.isfile(bezel) else None, remember=False)
        pad_art = self.st.get('pad_art', {}).get(f'{d.title_id:016x}')
        self.set_pad_art(pad_art if pad_art and os.path.isfile(pad_art) else None, remember=False)
        sound = self.st.get('boot_sound', {}).get(f'{d.title_id:016x}')
        self.set_game_sound(sound if sound and os.path.isfile(sound) else None, remember=False)
        save = self.st.get('save_in', {}).get(f'{d.title_id:016x}')
        self.set_save(save if save and os.path.isdir(save) else None, remember=False)
        threading.Thread(target=self.fetch_release, args=(d,), daemon=True).start()
        if saved is None:  # nothing chosen yet: its cover art, fetched off the window's thread
            threading.Thread(target=self.fetch_cover, args=(d, info['name']), daemon=True).start()
        self.refresh_files()
        self.remember_game(d, info['name'])
        if ask:  # a game picked again from the library keeps quiet
            self.run_flow(self.extras())

    # ---- the library: the games picked before, as the tiles on the Games page
    def remember_game(self, d, name):
        games = [g for g in self.st.get('library', []) if g['path'] != d.path]
        games.append(dict(path=d.path, name=name, id=f'{d.title_id:016x}', product=d.product or '', icon=icon_url(d.icon) if d.icon else ''))
        self.st['library'] = games[-LIBRARY_MAX:]
        save_settings(self.st)
        self.refresh_library()

    def refresh_library(self):
        """what the window shows of each remembered game: its name, its picture (the cover if this PC has it, else its own icon), and
        which one is open"""
        covers_dir = covers_folder()
        shown = []
        for g in self.st.get('library', []):
            art = (self.st.get('icon_art', {}).get(g['id']) or self.st.get('art', {}).get(g['id'])  # a square icon fills a tile best
                   or os.path.join(covers_dir, (covers.game_code(g['product']) or '-') + '.jpg'))
            cover = QUrl.fromLocalFile(art).toString() if os.path.isfile(art) else ''
            name = self.st.get('metadata', {}).get(g['id'], {}).get('name', g['name'])
            shown.append(dict(path=g['path'], name=name, icon=g['icon'], cover=cover, current=bool(self.game and self.game.path == g['path'])))
        self.set(library=shown)

    @Slot(str)
    def openRecent(self, path):
        if self._s['building'] or (self.game and self.game.path == path):
            return
        if not os.path.isfile(path):
            self.tell('El juego se ha movido', 'El archivo ya no está en su ubicación anterior. Vuelve a seleccionarlo en la página Compilar (Ctrl+O).', 'warn')
            self.forgetGame(path)
            return
        self.open_game(path, ask=False)

    @Slot(str)
    def forgetGame(self, path):
        self.st['library'] = [g for g in self.st.get('library', []) if g['path'] != path]
        save_settings(self.st)
        self.refresh_library()

    def refresh_note(self):
        info = pipeline.profile_for(self.game)
        if info:
            note = f'Probado en una Wii U real: {info["note"][0].lower() + info["note"][1:]}'
            if self.update:
                note += ' Se probó sin su actualización: debería funcionar igual con ella, pero todavía nadie lo ha comprobado.'
        else:
            note = 'Todavía nadie ha probado este juego en una Wii U: puede que no inicie o que se detenga a mitad de la ejecución.'
        if self.mod:
            note += ' Con un mod no está probado: el mod se incorpora antes de recompilar el juego, por lo que su código se ejecuta como el del propio juego.'
        self.set(note=note)

    def nearby(self):
        """updates and DLC for this game in the folder it came from"""
        found = {}
        folder = os.path.dirname(self.game.path)
        for n in sorted(os.listdir(folder))[:400]:
            p = os.path.join(folder, n)
            if p == self.game.path or not n.lower().endswith(DUMP_EXTS):
                continue
            try:
                d = dumps.Dump(p)
            except Exception:  # anything else in the folder
                continue
            if d.unique == self.game.unique and d.kind in ('update', 'dlc') and (d.kind not in found or d.version > found[d.kind].version):
                found[d.kind] = d
        return found

    def extras(self):
        """the questions after a game is chosen: its update, then its DLC"""
        found, name = self.nearby(), self._s['gameName']
        for kind, what, why in (('update', 'una actualización', 'la versión más reciente del juego, con sus correcciones'),
                                ('dlc', 'DLC', 'su contenido descargable: niveles, objetos y otros extras')):
            d = found.get(kind)
            if d:
                if (yield dict(title=f'Se encontró {what} para {name}', message=f'{describe(d)}  ·  {os.path.basename(d.path)}\n\n¿Quieres utilizarlo? Agrega {why}.',
                               yes='Usarla', no='No, gracias', tone='ask')):
                    self.set_extra(kind, d)
            elif (yield dict(title=f'¿Tienes {what} para {name}?', message=f'Utilizarlo agrega {why}. Selecciona «Lo tengo» para elegir el archivo.',
                             yes='Lo tengo', no='No', tone='ask')):
                self.chooseExtra(kind)
        if (yield dict(title=f'¿Tienes un mod para {name}?', message='Un mod creado para Luma en una 3DS: un .zip o una carpeta que contenga luma/titles/<title id>/ '
                       'en su interior (archivos nuevos, code.ips, code.bps o code.bin). Builder lo incorpora antes de recompilar. Los plugins (.3gx, como CTGP-7) '
                       'no se pueden incorporar.', yes='Tengo uno', no='No', tone='ask')):
            self.chooseMod('zip' if (yield dict(title='¿Un .zip o una carpeta?', message='¿En qué formato está el mod?', yes='Un .zip', no='Una carpeta', tone='ask'))
                           else 'folder')

    @Slot(str)
    def chooseExtra(self, kind):
        what = 'la actualización' if kind == 'update' else 'el DLC'
        path = QFileDialog.getOpenFileName(None, f'Selecciona {what} para {self._s["gameName"]}', os.path.dirname(self.game.path), DUMPS)[0]
        if not path:
            return
        d = self.open_dump(path)
        if d is None:
            return
        if d.kind != kind or d.unique != self.game.unique:
            self.tell(f'Archivo no válido', f'Ese archivo no corresponde a {what} de {self._s["gameName"]}.', 'error')
            return
        self.set_extra(kind, d)

    @Slot(str)
    def chooseMod(self, how):
        start = os.path.dirname(self.game.path)
        if how == 'zip':
            path = QFileDialog.getOpenFileName(None, f'Selecciona el mod para {self._s["gameName"]}', start, 'Mods (*.zip);;Todos los archivos (*.*)')[0]
        else:
            path = QFileDialog.getExistingDirectory(None, f'Carpeta del mod para {self._s["gameName"]}', start)
        if not path:
            return
        try:
            m = mods.Mod(path, self.game.title_id)
        except (mods.ModError, OSError) as e:
            self.tell('No se puede utilizar ese mod', str(e), 'error')
            return
        parts = ([f'{len(m.romfs)} archivos'] if m.romfs else []) + ([m.code.rsplit('/', 1)[-1]] if m.code else []) + (['exheader'] if m.exheader else [])
        m.close()
        self.set_extra('mod', path, ', '.join(parts))

    def ask_picture(self, title):
        """a picture the player picks, or None"""
        path = QFileDialog.getOpenFileName(None, title, self.st.get('art_folder', ''), 'Imágenes (*.png *.jpg *.jpeg *.bmp *.webp);;Todos los archivos (*.*)')[0]
        if not path:
            return None
        if QImage(path).isNull():
            self.tell("No se puede leer esa imagen", 'Selecciona una imagen PNG o JPEG.', 'error')
            return None
        self.st['art_folder'] = os.path.dirname(path)
        return path

    @Slot()
    def chooseArt(self):
        path = self.ask_picture(f'Imagen para {self._s["gameName"]}, por ejemplo, su portada')
        if path:
            self.set_art(path, self.icon_art if picked(self.icon_art) else None)  # an icon the player picked stays, a fetched one goes with the old art

    @Slot()
    def chooseIcon(self):
        path = self.ask_picture(f'Imagen cuadrada para el icono del menú de Wii U de {self._s["gameName"]}')
        if path:
            self.set_art(self.art, path)

    @Slot()
    def removeArt(self):
        self.set_art(None, removed=True)

    @Slot()
    def removeIcon(self):
        self.set_art(self.art, None)

    @Slot()
    def chooseBezel(self):
        path = self.ask_picture(f'Marco u overlay para el GamePad junto a las pantallas de {self._s["gameName"]}: cualquier imagen, ajustada para cubrirlo')
        if path:
            self.set_bezel(path)

    @Slot()
    def removeBezel(self):
        self.set_bezel(None)

    def set_bezel(self, path, remember=True):
        """the picture the GamePad shows where it shows no screen (tools/make_game_title.py, bezel_pixels), remembered for the game"""
        self.bezel = path
        if self.game and remember:
            self.keep_for_game('pad_bezel', path)
        self.set(bezelText=os.path.basename(path) if path else NO_BEZEL, hasBezel=bool(path))

    def keep_for_game(self, key, path):
        """a path chosen for the open game (settings[key][title id]), or forgotten with None"""
        chosen = self.st.setdefault(key, {})
        if path:
            chosen[f'{self.game.title_id:016x}'] = path
        else:
            chosen.pop(f'{self.game.title_id:016x}', None)
        save_settings(self.st)

    @Slot()
    def choosePadArt(self):
        path = self.ask_picture(f'Imagen exclusiva para el inicio de {self._s["gameName"]} en el GamePad, mostrada tal cual')
        if path:
            self.set_pad_art(path)

    @Slot()
    def removePadArt(self):
        self.set_pad_art(None)

    def set_pad_art(self, path, remember=True):
        """the GamePad's start-up screen of its own (tools/make_game_title.py, pad_art), remembered for the game"""
        self.pad_art = path
        if self.game and remember:
            self.keep_for_game('pad_art', path)
        self.set(padArtText=os.path.basename(path) if path else NO_PAD_ART, hasPadArt=bool(path),
                 padArtUrl=QUrl.fromLocalFile(path).toString() if path else '')

    @Slot()
    def editMetadata(self):
        """Edit the title and publisher shown by the Wii U and the 3DS information screen."""
        if not self.game or self._s['building']:
            return
        title, ok = QInputDialog.getText(None, 'Información del juego', 'Nombre del juego:', text=self._s['gameName'])
        if not ok:
            return
        publisher, ok = QInputDialog.getText(None, 'Información del juego', 'Desarrollador o distribuidor:', text=self._s['maker'])
        if not ok:
            return
        title = ' '.join(title.split())
        publisher = ' '.join(publisher.split())
        if not title:
            self.tell('Falta el nombre del juego', 'Escribe un nombre para mostrarlo en el menú y la pantalla de inicio.', 'warn')
            return
        key = f'{self.game.title_id:016x}'
        self.st.setdefault('metadata', {})[key] = {'name': title, 'publisher': publisher}
        save_settings(self.st)
        self.set(gameName=title, maker=publisher)
        self.refresh_library()

    @Slot()
    def chooseGameSound(self):
        sound = os.path.join(GAME_SOUNDS, f'{self.game.title_id:016x}.btsnd')
        if self.ask_sound(f'Sonido para reproducir al iniciar {self._s["gameName"]}', sound):
            self.set_game_sound(sound)

    @Slot()
    def removeGameSound(self):
        if self.game_sound:
            try:
                os.remove(self.game_sound)
            except OSError:
                pass  # forgotten either way: the build goes back to the sound for every game
        self.set_game_sound(None)

    def set_game_sound(self, path, remember=True):
        """the game's own start-up sound, a .btsnd made from the player's WAV (tools/make_boot_sound.py), remembered for the game"""
        self.game_sound = path
        if self.game and remember:
            self.keep_for_game('boot_sound', path)
        self.set(gameSoundText='Propio' if path else NO_GAME_SOUND, hasGameSound=bool(path))

    @Slot()
    def bringSave(self):
        """a 3DS save to pack into the game's build, which the game takes in on the console (saves.py)"""
        chosen = QFileDialog.getExistingDirectory(None, f"Carpeta con la partida de {self._s['gameName']}: de Checkpoint, JKSM o un emulador",
                                                  self.st.get('save_folder', ''))
        if not chosen:
            return
        self.st['save_folder'] = os.path.dirname(chosen)
        save_settings(self.st)
        try:
            saves.checked(chosen)
        except (saves.SaveError, OSError) as e:
            self.tell("No se puede utilizar esa carpeta", str(e), 'error')
            return
        self.set_save(chosen)

    @Slot()
    def removeSave(self):
        self.set_save(None)

    def set_save(self, path, remember=True):
        """the folder of the save to pack into the build, remembered for the game"""
        self.save_in = path
        if self.game and remember:
            self.keep_for_game('save_in', path)
        self.set(saveText=os.path.basename(os.path.normpath(path)) if path else NO_SAVE, hasSave=bool(path))

    def fetch_cover(self, game, name):
        """on a thread of its own: the game's banner and icon from SteamGridDB where the player gave a key, its cover from GameTDB otherwise
        and for what SteamGridDB lacks (covers.py), handed to the window's thread when they're here"""
        folder, title_id, key = covers_folder(), f'{game.title_id:016x}', self.st.get('sgdb_key')
        found = {}
        try:
            # a game SteamGridDB doesn't know by this region's name is looked for by the others' (GameTDB's list): Luigi's Mansion 2 is Dark Moon
            also = lambda: covers.other_names(game.product, folder)  # noqa: E731
            found = (covers.steamgriddb(name, key, folder, title_id, also=also) if key and key != self.refused_key else None) or {}
        except covers.BadKey:
            self.refused_key = key  # not asked with again until it's changed, and said once
            self.keyRefused.emit()
        cover = None if found.get('banner') and found.get('icon') else covers.cover(game.product, folder)
        banner, icon = found.get('banner') or cover, found.get('icon') or (cover if found.get('banner') else None)
        if banner or icon:
            self.coverReady.emit(title_id, banner or '', icon or '')

    def fetch_release(self, game):
        """Fetch release metadata even when the user already supplied custom artwork."""
        try:
            released = covers.released(game.product, covers_folder())
        except (OSError, ValueError):
            released = ''
        self.releaseReady.emit(f'{game.title_id:016x}', released)

    @Slot(str, str)
    def release_ready(self, title_id, released):
        if self.game and f'{self.game.title_id:016x}' == title_id:
            self.set(releaseText=released)

    def cover_ready(self, title_id, path, icon):
        # fetched art where the game has none; an icon the player picked stays
        if self.game and f'{self.game.title_id:016x}' == title_id and not self.art and not self._s['building']:
            self.set_art(path or None, self.icon_art if picked(self.icon_art) else (icon or None))
        self.refresh_library()

    def key_refused(self):
        self.tell("SteamGridDB rechazó la clave", 'Comprueba la clave en la página Ayuda, en Imagen y sonido: es el código largo de steamgriddb.com, en '
                  'Preferences > API. Hasta entonces, las imágenes se obtendrán de GameTDB.', 'warn')

    def set_art(self, path, icon=None, remember=True, removed=False):
        """the pictures this game's start-up screens and Wii U Menu icon are made from, remembered for the game: path for both, or for the
        screens alone where icon is the icon's. removed: the player took the art away, remembered as '' so none is fetched again"""
        self.art, self.icon_art = path, icon
        if self.game and remember:
            title, arts, icons = f'{self.game.title_id:016x}', self.st.setdefault('art', {}), self.st.setdefault('icon_art', {})
            if path or removed:
                arts[title] = path or ''
            else:
                arts.pop(title, None)  # none chosen yet: the next time the game opens, its art is looked for again
            if icon:
                icons[title] = icon
            else:
                icons.pop(title, None)
            save_settings(self.st)
        url = lambda p: QUrl.fromLocalFile(p).toString() if p else ''  # noqa: E731
        self.set(artText=source(path) if path else NO_ART, hasArt=bool(path or icon), artUrl=url(path),
                 iconText=source(icon) if icon else NO_ICON_ART if path else NO_ART, hasIconArt=bool(icon), iconArtUrl=url(icon),
                 tileUrl=url(icon or path))  # the Build page's tile: a square icon fills it best
        if remember:
            self.refresh_library()

    @Slot()
    def setArtKey(self):
        key, ok = QInputDialog.getText(None, 'SteamGridDB', 'Tu clave API de SteamGridDB: el código largo que aparece en steamgriddb.com, en Preferences > API.\n'
                                       "La clave permanece en este PC, en los ajustes de Builder. Déjala vacía para dejar de consultar SteamGridDB.")
        if not ok:
            return
        self.st['sgdb_key'] = key.strip()
        save_settings(self.st)
        self.set(artKeySet=bool(self.st['sgdb_key']))
        removed = self.game and self.st.get('art', {}).get(f'{self.game.title_id:016x}') == ''  # on purpose: it stays removed
        if self.game and self.st['sgdb_key'] and not self._s['building'] and not removed and (not self.art or source(self.art) == FROM_TDB):
            # the open game's fetched art, again from the new place; a picture the player chose stays
            self.set_art(None, self.icon_art if picked(self.icon_art) else None, remember=False)
            threading.Thread(target=self.fetch_cover, args=(self.game, self._s['gameName']), daemon=True).start()

    def ask_sound(self, title, to):
        """a sound the player picks, made into a start-up sound at to (tools/make_boot_sound.py); False if none, or after saying why not"""
        path = QFileDialog.getOpenFileName(None, title, self.st.get('sound_folder', ''), 'Sonidos (*.wav *.btsnd);;Todos los archivos (*.*)')[0]
        if not path:
            return False
        self.st['sound_folder'] = os.path.dirname(path)
        save_settings(self.st)
        try:
            data = pipeline.tool('make_boot_sound').boot_sound(path)
            os.makedirs(os.path.dirname(to), exist_ok=True)
            with open(to, 'wb') as f:
                f.write(data)
        except (OSError, ValueError, EOFError, ImportError) as e:
            self.tell("No se puede utilizar ese sonido", f'{e}. Selecciona un archivo WAV: la mayoría de los programas de audio pueden guardarlo en ese formato.', 'error')
            return False
        return True

    @Slot()
    def chooseSound(self):
        if self.ask_sound('Sonido para reproducir al iniciar tus juegos', BOOT_SOUND):
            self.set(soundSet=True, soundCustom=True)

    @Slot()
    def removeSound(self):
        try:
            if os.path.isfile(BOOT_SOUND):
                os.remove(BOOT_SOUND)
        except OSError as e:
            self.tell("No se pudo quitar el sonido", str(e), 'error')
        self.set(soundSet=os.path.isfile(BOOT_SOUND) or os.path.isfile(os.path.join(pipeline.ROOT, 'phase0', 'title_code', 'meta', 'bootSound.btsnd')),
                 soundCustom=os.path.isfile(BOOT_SOUND))

    @Slot(str)
    def removeExtra(self, kind):
        self.set_extra(kind, None)

    def set_extra(self, kind, d, what=''):
        setattr(self, kind, d)
        if kind == 'mod':
            self.set(modText=f'{what}  ·  {os.path.basename(d)}' if d else 'Ninguno', hasMod=d is not None)
            if self.game:
                self.refresh_note()
            return
        text = f'{describe(d)}  ·  {os.path.basename(d.path)}' if d else 'Ninguno'  # the window cuts it to fit
        if kind == 'update':
            self.set(updateText=text, hasUpdate=d is not None)
            if self.game:
                self.refresh_note()
        else:
            self.set(dlcText=text, hasDlc=d is not None)

    @Slot()
    def chooseKeys(self):
        p = QFileDialog.getExistingDirectory(None, 'Carpeta con las claves de tu 3DS (boot9.bin, seeddb.bin)', self.st['keys'])
        if p:
            if not decrypt.have_keys(p):
                self.tell('No se encontraron las claves', 'Esa carpeta no contiene boot9.bin.', 'warn')
            self.st['keys'] = p
            decrypt.use_keys(p)
            if self.st.get('system') and not dumps.have_system_archives(pipeline.ARCHIVES):
                self.import_system(self.st['system'])  # a 3DS's own copy, chosen before the keys that open it
            self.refresh_files()

    @Slot()
    def chooseSystem(self):
        p = QFileDialog.getExistingDirectory(None, 'Archivos del sistema de tu 3DS: carpeta de usuario de Azahar o carpeta 0004009b copiada desde tu 3DS con GodMode9',
                                             self.st.get('system', ''))
        if p:
            if not self.import_system(p):
                if not self.system_locked:
                    self.tell('No se encontraron archivos del sistema de 3DS', HELP['system'][1], 'warn')
                elif decrypt.have_keys(self.st['keys']):
                    self.tell('No se pudieron abrir esos archivos', f'Esa es la carpeta correcta, pero no se pudieron descifrar sus archivos con el boot9.bin de '
                              f'{self.st["keys"]}. Se pueden abrir con las claves de la misma 3DS desde la que se copiaron.', 'warn')
                else:
                    self.tell('Esos archivos necesitan las claves de tu 3DS', 'Esa es la carpeta correcta. Una 3DS conserva estos archivos cifrados y se pueden abrir con '
                              'boot9.bin de tu propia 3DS.\n\nPulsa «Carpeta» junto a Claves de 3DS y selecciona la carpeta que contiene boot9.bin: '
                              'Builder leerá automáticamente los archivos del sistema desde aquí.', 'warn')
            self.refresh_files()

    @Slot(str)
    def explain(self, what):
        self.tell(*HELP[what])

    def import_system(self, folder):
        """the system archives from a folder into the ones every build takes (pipeline.ARCHIVES), once: they stay for the next games"""
        tmp = tempfile.mkdtemp(prefix='3diiu_sys_')  # for contents that need decrypting first
        self.system_locked = False  # the folder has them, as a 3DS keeps them, and they didn't decrypt: two testers were told "none there"
        try:
            got = dumps.import_system_archives(folder, pipeline.ARCHIVES, tmp)
        except decrypt.DecryptError:
            got, self.system_locked = [], True
        except (OSError, dumps.DumpError):
            got = []
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
        if got or self.system_locked:  # a locked folder is kept: it's read again once the keys are chosen
            self.st['system'] = folder
            save_settings(self.st)
        return got

    def find_system(self, everywhere):
        """the system files from an emulator's folder on this PC, if the builder has none yet"""
        for folder in [] if dumps.have_system_archives(pipeline.ARCHIVES) else emulator_folders(everywhere):
            if self.import_system(folder) and dumps.have_system_archives(pipeline.ARCHIVES):
                break
        self.refresh_files()

    def came_back(self, state):
        """Back in this window, from Azahar's File > Set Up System Files say: what that left is taken without being pointed at, as the help
        says it is. Only the emulators' own folders are looked in, which costs nothing."""
        if state == Qt.ApplicationState.ApplicationActive and not self._s['building']:
            self.find_system(False)

    @Slot()
    def chooseOtp(self):
        p = QFileDialog.getOpenFileName(None, 'Archivo otp.bin de tu Wii U', os.path.dirname(self.st.get('otp', '')), 'otp.bin (otp.bin);;Todos los archivos (*.*)')[0]
        if p:
            self.st['otp'] = p
            self.refresh_files()

    @Slot(str)
    def setTarget(self, t):
        self.st['target'] = t
        self.set(target=t)
        self.refresh_files()

    @Slot()
    def chooseFolder(self):
        p = QFileDialog.getExistingDirectory(None, 'Dónde guardar el WUP', self.st.get('folder', ''))
        if p:
            self.st['folder'] = p
            self.setTarget('folder')

    @Slot(str)
    def pickCard(self, path):
        self.st['card'] = path
        self.setTarget('card')

    @Slot()
    def refreshCards(self):
        self.refresh_cards()
        self.refresh_files()

    def refresh_cards(self):
        cards = sd_cards()
        if self.st.get('card') not in cards:
            self.st['card'] = cards[0] if cards else ''
        self.set(cards=[dict(path=c, label=f'{c}    {"preparada para Wii U" if os.path.isdir(os.path.join(c, "wiiu")) else "unidad extraíble"}') for c in cards])

    def refresh_files(self):
        keys, otp = self.st['keys'], self.st.get('otp', '')
        needed = bool(self.game and self.game.encrypted())
        card, folder = self.st.get('card', ''), self.st.get('folder', '')
        if self._s['target'] == 'card':
            dest = f'{card}   {"Tarjeta de Wii U" if os.path.isdir(os.path.join(card, "wiiu")) else "Tarjeta SD"}' if card else 'No se detectó ninguna tarjeta SD'
        else:
            dest = short(folder, 40) if folder else 'Selecciona una carpeta...'
        have = decrypt.have_keys(keys)
        system = dumps.have_system_archives(pipeline.ARCHIVES)
        source = short(self.st['system'], 26) if self.st.get('system') else ''
        self.set(sysOk=system, sysText=(source or 'Configurados') if system else 'No configurados')  # short: the row has its pills beside it
        self.set(keysOk=have, keysNeeded=needed and not have,
                 keysText=short(keys, 26) if have else 'No configuradas' if needed else 'Solo para volcados cifrados',
                 otpOk=otp_ok(otp), otpText=short(otp, 26) if otp_ok(otp) else 'No seleccionado', destText=dest)

    # ---- updates (updater.py): every call that reaches the network or the disk runs on a worker, which reports through updateEvent
    @Slot()
    def windowShown(self):
        """The window's first frame, or run()'s timer when no frame comes: tells updater a downloaded version came up (or every update would
        be set aside after its first use), then, in a released app only, looks for a newer release a few seconds later"""
        if self.shown:
            return
        self.shown = True
        try:  # create()'s connection to every frame: the first was the one wanted
            QObject.disconnect(self.frames)
        except Exception:  # started() must run whatever happens to the connection
            pass
        threading.Thread(target=updater.started, daemon=True).start()  # it waits on the launch record's lock
        if FROZEN and updater.tag_key(self._s['releaseTag']):  # a development build checks only when asked
            self.auto_check.start()

    def set_aside(self, failed):
        """whether a release newer than this app failed to start here: older failures it has moved past aren't worth a note"""
        mine = updater.tag_key(self._s['releaseTag'])
        return any(mine is None or updater.tag_key(t) > mine for t in failed)

    @Slot()
    def checkUpdate(self):
        self.check_update(True)

    def check_update(self, asked):
        """asked: the person pressed Check for updates, so a release that failed to start here before is offered again"""
        if self._s['updateState'] in ('downloading', 'unpacking', 'ready', 'restarting'):
            return  # the About line says what is under way
        if self.check_job and self.check_job.is_alive():
            if asked:  # the automatic check is under way: an asked one follows it, so its answer is shown
                self.ask_again = True
                self.set(updateState='checking', updateMessage='')
            return
        if asked:
            self.set(updateState='checking', updateMessage='')
        own = self._s['releaseTag']

        def job():
            release = updater.check(own, skip_failed=False) if asked else updater.check(own)
            self.updateEvent.emit('checked', (release, asked, updater.failed_tags(), updater.activated()))
        self.check_job = threading.Thread(target=job, daemon=True)
        self.check_job.start()

    @Slot()
    def startUpdate(self):
        """Update, or Try again: the whole update, which also clears a failed mark left on that release"""
        if self._s['updateState'] not in ('available', 'failed') or (self.update_job and self.update_job.is_alive()):
            return
        if not self.release:  # a version made ready before this start, whose restart failed: find its release again
            self.check_update(True)
            return
        release = self.release
        self.update_stop.clear()
        self.set(updateState='downloading', updateProgress=0, updateMessage='')

        def job():
            shown = [-1]

            def progress(done, size):
                percent = min(100, done * 100 // max(1, size))
                if percent != shown[0]:  # a signal per percent, not per piece
                    shown[0] = percent
                    self.updateEvent.emit('progress', percent)
            try:
                zip_path = updater.download(release, updater.home(), progress, self.update_stop.is_set)
                if zip_path is None:
                    self.updateEvent.emit('cancelled', None)
                    return
                self.updateEvent.emit('unpacking', None)
                updater.activate(updater.unpack(zip_path, updater.home()))
                self.updateEvent.emit('ready', updater.failed_tags())
            except updater.UpdateError as e:
                self.updateEvent.emit('failed', str(e))
            except Exception as e:  # anything else is said the same way, and the app carries on
                self.updateEvent.emit('failed', f'La actualización se detuvo: {e}')
        self.update_job = threading.Thread(target=job, daemon=True)
        self.update_job.start()

    @Slot()
    def cancelUpdate(self):
        self.update_stop.set()

    @Slot()
    def laterUpdate(self):
        self.set(updateState='')

    @Slot()
    def restartForUpdate(self):
        if self._s['updateState'] != 'ready':
            return
        if self._s['building']:
            self.tell('Hay una compilación en curso', 'Reinicia cuando termine la compilación o cancélala primero.', 'warn')
            return
        self.set(updateState='restarting', updateMessage='Iniciando la nueva versión...')  # no build starts meanwhile

        def job():
            exe = updater.current(updater.own_tag())
            self.updateEvent.emit('quit' if exe and updater.hand_over(exe, []) else 'restart failed', None)
        self.update_job = threading.Thread(target=job, daemon=True)
        self.update_job.start()

    @Slot(str, object)
    def update_event(self, what, value):
        """a worker's result, on the window's thread"""
        if what == 'checked':
            self.check_job = None  # its last act was to send this: a check asked for meanwhile can start, though the thread may not have ended
            release, asked, failed, ready = value
            self.set(updateSetAside=self.set_aside(failed))
            if self.ask_again and not asked:
                self.ask_again = False
                self.check_update(True)
                return
            if self._s['updateState'] in ('downloading', 'unpacking', 'ready', 'restarting'):
                return
            if release:
                self.release = release
                self.set(updateState='ready' if release.tag == ready else 'available', updateTitle=release.title,
                         updateNotes=release.notes[:400], updateMessage='')  # ready: downloaded already, and Later was pressed
            elif asked:
                self.set(updateState='none')
        elif what == 'progress':
            self.set(updateProgress=value)
        elif what == 'cancelled':
            self.set(updateState='available', updateProgress=0)
        elif what == 'unpacking':
            self.set(updateState='unpacking', updateProgress=100)
        elif what == 'ready':
            self.set(updateState='ready', updateSetAside=self.set_aside(value), updateMessage='')
        elif what == 'failed':
            self.set(updateState='failed', updateMessage=value)
        elif what == 'restart failed':
            self.set(updateState='failed', updateMessage='La nueva versión no se inició, por lo que esta versión seguirá ejecutándose.')
        elif what == 'quit':
            self.stop_for_close()  # a build left running would go on behind the new window
            QApplication.quit()

    # ---- building
    @Slot()
    def build(self):
        if not self.game or self._s['building'] or self._s['updateState'] == 'restarting':
            return
        if not self._s['rootOk']:
            self.tell('Falta el motor de compilación', 'La carpeta engine, junto a 3Dii U Builder.exe, contiene el recompilador y su cadena de herramientas. Vuelve a copiar '
                                                     'toda la carpeta 3Dii U Builder, incluida la carpeta engine.', 'error')
            return
        if not otp_ok(self.st.get('otp', '')):
            self.tell('Falta otp.bin', 'Selecciona el otp.bin de tu Wii U: debe tener exactamente 1024 bytes.', 'warn')
            return
        dest = self.st.get('card', '') if self._s['target'] == 'card' else self.st.get('folder', '')
        if not dest or not os.path.isdir(dest):
            self.tell('¿Dónde quieres guardarlo?', 'Selecciona una tarjeta SD o una carpeta para el juego.', 'warn')
            return
        self.find_system(True)
        if not dumps.have_system_archives(pipeline.ARCHIVES):
            self.tell('Faltan los archivos del sistema de 3DS', HELP['system'][1], 'warn')
            return
        save_settings(self.st)
        argv = [sys.executable, '--pipeline'] if FROZEN else [sys.executable.replace('pythonw.exe', 'python.exe'), '-u', os.path.join(HERE, 'pipeline.py')]
        argv += ['--game', self.game.path, '--otp', self.st['otp'], '--card' if self._s['target'] == 'card' else '--out', dest, '--keys', self.st['keys']]
        for kind in ('update', 'dlc'):
            if getattr(self, kind):
                argv += [f'--{kind}', getattr(self, kind).path]
        if self.mod:
            argv += ['--mod', self.mod]
        argv += ['--art-style', self._s['artStyle']]
        if self.art:
            argv += ['--art', self.art]
        if self._s['artStyle'] == 'console':
            argv += ['--tdb', covers_folder()]
        argv += ['--name', self._s['gameName'], '--publisher', self._s['maker']]
        if self.icon_art:
            argv += ['--icon-art', self.icon_art]
        if self.bezel:
            argv += ['--pad-bezel', self.bezel]
        if self.pad_art:
            argv += ['--pad-art', self.pad_art]
        if self.game_sound:
            argv += ['--boot-sound', self.game_sound]
        if self.save_in:
            argv += ['--save', self.save_in]
        if self._s['isMk7'] and self._s['mk7Online']:
            argv += ['--mk7-online']
        self.log_lines, self.result, self.cancelled = [], None, False
        stages = [n for n, _ in pipeline.STAGES if n != 'Copiando' or self._s['target'] == 'card']
        self.set(building=True, view='building', stages=stages, stage=0, failed=False, progress=0.0, detail='Iniciando...', log='')
        self.proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, encoding='utf-8', errors='replace',
                                     creationflags=NO_WINDOW, start_new_session=not WIN, env=dict(os.environ, PYTHONIOENCODING='utf-8'))
        threading.Thread(target=self.read, args=(self.proc,), daemon=True).start()
        self.timer.start(100)

    def read(self, proc):
        for line in proc.stdout:
            self.lines.put(line.rstrip('\n'))
        proc.wait()
        self.lines.put(None)

    def poll(self):
        done, logged = False, False
        try:
            while True:
                line = self.lines.get_nowait()
                if line is None:
                    done = True
                    break
                if line.startswith('@stage '):
                    name = line[7:]
                    if name in self._s['stages']:
                        self.set(stage=self._s['stages'].index(name))
                elif line.startswith('@progress '):
                    self.set(progress=float(line.split()[1]) / 100)  # the pipeline's 0-100; the window's bars go 0 to 1
                elif line.startswith(('@done ', '@error ')):
                    self.result = line
                else:
                    self.log_lines.append(line)
                    logged = True
                    if line.strip():
                        self.set(detail=short(line.strip(), 96, end=True))
        except queue.Empty:
            pass
        if logged:
            self._s['log'] = '\n'.join(self.log_lines[-500:])
            self.logChanged.emit()
        if not done:
            return
        self.timer.stop()
        if self.result and self.result.startswith('@done '):
            where = self.result[6:]
            msg = (f'{self._s["gameName"]} se guardó en tu tarjeta SD, en install\\{os.path.basename(where)}. Inserta la tarjeta en tu Wii U e instálalo con WUP '
                   f'Installer GX2.' if self._s['target'] == 'card' else
                   f'El paquete WUP se guardó en {where}. Cópialo a la carpeta install de tu tarjeta SD e instálalo con WUP Installer GX2.')
            self.set(building=False, view='done', stage=len(self._s['stages']), where=where,
                     resultText=msg + ' Si ya tenías instalada una versión anterior de este juego, elimínala primero desde la consola.')
        elif self.cancelled:
            self.set(building=False, view='form')
        else:
            self.set(building=False, view='failed', failed=True,
                     resultText=self.result[7:] if self.result else 'La compilación se detuvo. Pulsa «Detalles» para ver el registro.')

    @Slot()
    def cancel(self):
        if self.proc and self.proc.poll() is None:
            self.cancelled = True
            self.set(detail='Deteniendo...')
            if WIN:  # the build's own compilers go too
                subprocess.run(['taskkill', '/T', '/F', '/PID', str(self.proc.pid)], creationflags=NO_WINDOW, capture_output=True)
            else:
                import signal
                os.killpg(os.getpgid(self.proc.pid), signal.SIGTERM)

    @Slot()
    def back(self):
        self.set(view='form')

    @Slot()
    def openFolder(self):
        where = self._s['where']
        if os.path.isdir(where):
            os.startfile(where) if WIN else subprocess.Popen(['open' if sys.platform == 'darwin' else 'xdg-open', where])

    @Slot(result=bool)
    def busy(self):
        return bool(self.proc and self.proc.poll() is None)

    def stop_for_close(self):
        """the app is quitting: a build and an update download stop with it"""
        self.cancel()
        self.cancelUpdate()


def window_qml(layout):
    """the window the settings' layout asks for: the simple one, or the modern one"""
    return QUrl.fromLocalFile(os.path.join(HERE, 'ui', 'Simple.qml' if layout == 'simple' else 'Main.qml'))


def place(win):
    """a window sized to its screen's free area and centred on it: the modern one 16:9 up to 1280 wide, the simple one at its own size"""
    area = win.screen().availableGeometry()  # its own screen's free area, not the whole desktop: "Build It!" stays above the taskbar
    if win.objectName() == 'simple':
        w, h = min(win.width(), area.width()), min(win.height(), area.height())
        win.setMinimumSize(QSize(min(640, area.width()), min(480, area.height())))
    else:
        w = int(min(1280, area.width() * 0.9, area.height() * 0.9 * 16 / 9))
        h = w * 9 // 16
        win.setMinimumSize(QSize(min(880, area.width()), min(495, area.height())))
    win.setGeometry(area.x() + (area.width() - w) // 2, area.y() + (area.height() - h) // 2, w, h)


def create(argv=None):
    """the application, its window and the App behind it, not yet running"""
    QApplication.setHighDpiScaleFactorRoundingPolicy(Qt.HighDpiScaleFactorRoundingPolicy.PassThrough)
    qt = QApplication(argv or sys.argv)
    qt.setApplicationName(APP)
    qt.setWindowIcon(QIcon(QPixmap.fromImage(draw_icon(256))))
    if WIN:
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID('3DiiU.Builder')  # the taskbar shows this app's icon, not Python's
    state = App()
    qt.applicationStateChanged.connect(state.came_back)
    engine = QQmlApplicationEngine()
    engine.rootContext().setContextProperty('app', state)
    engine.rootContext().setContextProperty('T', theme())
    engine.rootContext().setContextProperty('appIcon', png_url(draw_icon(512)))  # the rail shows it at 162: 512 stays sharp up to a 4K window
    engine.load(window_qml(state.st.get('layout')))
    if not engine.rootObjects():  # the saved layout's window didn't load: the modern one, so the app always shows one
        state.st['layout'] = 'modern'
        save_settings(state.st)
        engine.load(window_qml('modern'))
    for win in engine.rootObjects():  # its first frame on screen is when it has really been shown (App.windowShown)
        quick = shiboken6.wrapInstance(shiboken6.getCppPointer(win)[0], QQuickWindow)
        # it comes at every frame, from the render thread: windowShown ends the connection at the first
        state.frames = quick.frameSwapped.connect(state.windowShown, Qt.ConnectionType.QueuedConnection)
    return qt, engine, state


def run():
    qt, engine, state = create()
    if not engine.rootObjects():
        return 1
    win = engine.rootObjects()[0]  # loaded hidden, so a check can load it without showing it
    place(win)
    win.show()

    def swap(layout):  # the other window, shown before this one goes, so the app never has no window
        old = engine.rootObjects()[-1]
        engine.load(window_qml(layout))
        new = engine.rootObjects()[-1]
        if new is old:  # it didn't load: the window stays as it was
            return
        place(new)
        new.show()
        old.close()
        old.deleteLater()
    state.layoutWanted.connect(swap)
    # The first frame is the sign the window came up. A window started minimised draws none, so a timer says it for one that isn't showing.
    # One that is showing and has still drawn nothing is broken, and is left unconfirmed: updater sets that version aside.
    qt.aboutToQuit.connect(state.stop_for_close)  # not a swap: the new window is shown before the old one closes
    QTimer.singleShot(10000, lambda: None if not engine.rootObjects() or engine.rootObjects()[-1].isExposed() else state.windowShown())
    code = qt.exec()
    del engine  # before the App it shows, or its bindings read a gone app on the way out
    return code
