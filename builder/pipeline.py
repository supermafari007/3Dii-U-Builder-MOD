#!/usr/bin/env python3
"""Turns a 3DS game dump into a WUP for WUP Installer GX2, with its update and DLC if given. The app (builder/app.py) runs this; it works on its own too:

    pipeline.py --game <dump> [--update <dump>] [--dlc <dump>] [--mod <folder or .zip> ...] [--keys <folder>] [--emulator <folder>] --otp <otp.bin> (--card D: | --out <folder>) [--keep] [--jobs N]

--keys: the folder with boot9.bin and seeddb.bin from your own 3DS, ~/.3ds by default; only encrypted dumps need it, and they're decrypted the way
cia_to_decrypted_3ds.py does it (decrypt.py). --emulator: a 3DS emulator's user folder (Azahar, Citra) or the 0004009b folder GodMode9 copies off
your 3DS, for the system archives (shared font, Mii data) if this PC has none yet. --otp: your Wii U's otp.bin, for the common key the WUP is
packed with.
Lines starting with @ are for the app: @stage <name>, @progress <0-100>, @done <where it went>, @error <why>.

It builds with tools/win/build3ds.py, in folders of its own (builder_work/, build_win_builder/), so it never touches the nine ports' builds."""
import argparse
import filecmp
import glob
import io
import json
import os
import re
import shutil
import subprocess
import sys
import threading
import types

_HERE_RAW = os.path.dirname(os.path.abspath(__file__))
HERE = os.path.dirname(_HERE_RAW) if _HERE_RAW.lower().endswith('.zip') else _HERE_RAW
ROOT = os.environ.get('THREEDII_ROOT') or os.path.dirname(HERE)  # the standalone app runs from elsewhere: app.py finds the repository
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools', 'win'))
import credits  # noqa: E402
import decrypt  # noqa: E402
import dumps  # noqa: E402
import mods  # noqa: E402
import shaderlogs  # noqa: E402
import user_files  # noqa: E402

WORK = os.environ.get('THREEDII_WORK') or ROOT  # the standalone app's engine may be read-only: then it builds in a folder of the user's
WORK_GAMES = os.path.join(WORK, 'builder_work')
ARCHIVES = os.path.join(WORK, 'system_archives') if WORK != ROOT else os.path.join(ROOT, 'games', 'system_archives')
STAGES = [('Leyendo el juego', 2), ('Descifrando', 8), ('Extrayendo archivos', 7), ('Buscando el código del juego', 2), ('Recompilando', 48), ('Shaders', 14),
          ('Creando el título', 2), ('Empaquetando el WUP', 10), ('Copiando', 7)]


class Progress:
    """@stage and @progress lines over the whole run, from each stage's share"""
    def __init__(self):
        self.done, self.share = 0, 0

    def stage(self, name):
        for n, (s, w) in enumerate(STAGES):
            if s == name:
                self.done = sum(x for _, x in STAGES[:n])
                self.share = w
        print(f'@stage {name}', flush=True)
        self.at(0)

    def at(self, fraction):
        print(f'@progress {min(100, self.done + self.share * max(0.0, min(1.0, fraction))):.1f}', flush=True)


class Watch:
    """build3ds prints 'game code: 120 of 1300, 50 s': that's the recompile stage's progress"""
    def __init__(self, out, progress):
        self.out, self.progress, self.parts = out, progress, ''

    def write(self, s):
        self.out.write(s)
        self.parts += s
        while '\n' in self.parts:
            line, self.parts = self.parts.split('\n', 1)
            m = re.search(r'(runtime|game code): (\d+) of (\d+)', line)
            if m:  # the runtime is the first third, and only compiles once
                f = int(m.group(2)) / int(m.group(3))
                self.progress.at(f * 0.3 if m.group(1) == 'runtime' else 0.3 + f * 0.7)
        return len(s)

    def flush(self):
        self.out.flush()


class Held:
    """stdout while the shaders are made alongside the code: what that work prints (build3ds.context.tag 'shaders', handed down to its workers)
    waits here until the Shaders stage, as the app shows one stage at a time"""
    def __init__(self, out, context):
        self.out, self.context, self.held = out, context, io.StringIO()

    def write(self, s):
        return (self.held if getattr(self.context, 'tag', None) == 'shaders' else self.out).write(s)

    def flush(self):
        self.out.flush()


def fail(why):
    print(f'@error {why}', flush=True)
    sys.exit(1)


def check_compiler_policy():
    """Check the bundled PowerPC compiler before spending time unpacking a game.

    Windows security policy can block either the driver (g++.exe) or its C++
    frontend (cc1plus.exe). Checking both here turns a late/repeated compile
    failure into one actionable error at the start of the build.
    """
    exe = '.exe' if os.name == 'nt' else ''
    devkitpro = os.environ.get('THREEDII_DEVKITPRO') or ('C:/devkitPro/opt/devkitpro' if os.name == 'nt' else '/opt/devkitpro')
    compiler = os.path.join(devkitpro, 'devkitPPC', 'bin', 'powerpc-eabi-g++' + exe)
    frontends = sorted(glob.glob(os.path.join(devkitpro, 'devkitPPC', 'libexec', 'gcc', 'powerpc-eabi', '*', 'cc1plus' + exe)))
    if not os.path.isfile(compiler):
        fail(f'No se encuentra powerpc-eabi-g++{exe}, el compilador de Wii U. Repara o vuelve a extraer el paquete completo de 3Dii U Builder.')
    if not frontends:
        fail(f'No se encuentra cc1plus{exe}, el compilador C++ de devkitPPC. Repara o vuelve a extraer el paquete completo de 3Dii U Builder.')
    probes = [(compiler, 'powerpc-eabi-g++' + exe), (frontends[-1], 'cc1plus' + exe)]
    for executable, label in probes:
        try:
            result = subprocess.run([executable, '--version'], capture_output=True, text=True, errors='replace', timeout=15,
                                    creationflags=0x08000000 if os.name == 'nt' else 0)
        except (OSError, subprocess.TimeoutExpired) as e:
            fail(f'Windows no permite iniciar {label}; la compilación se detuvo antes de extraer el juego. '
                 'Revisa Seguridad de Windows > Historial de protección para ver si bloqueó este compilador. '
                 'Si el archivo proviene del paquete oficial de 3Dii U Builder, restaura o permite ese archivo desde el historial; '
                 'no desactives la protección completa. Si es un equipo administrado, solicita al administrador que revise el bloqueo. '
                 f'Detalle: {e}')
        if result.returncode:
            fail(f'No se pudo iniciar {label}; la compilación se detuvo antes de extraer el juego. '
                 'Revisa Seguridad de Windows > Historial de protección y el registro de Integridad de código. '
                 f'Detalle: {(result.stderr or result.stdout).strip()[:400]}')


def profile_for(dump):
    compat = json.load(open(os.path.join(HERE, 'compat.json'), encoding='utf-8'))
    return compat.get('%016X' % dump.title_id)


def derived(dump):
    """title id, product code, slug and folder for a game the list doesn't have: kept clear of real Wii U titles (high byte 0x3D), and the 3DS id stays readable"""
    code = (dump.product or 'CTR-X-XXXX').split('-')[-1][:4].upper().ljust(4, 'X')
    folder = re.sub(r'[^A-Za-z0-9]+', '_', dump.name or code).strip('_') or code
    return {'slug': 'g_' + code.lower(), 'title_id': '00050000%08X' % (0x3D000000 | (dump.unique >> 8)), 'name': dump.name or code, 'product': 'WUP-N-' + code,
            'folder': folder, 'status': 'Untested', 'note': 'Not tested on a console yet: it may not start, or stop partway.'}


def console_learned(slug):
    """What consoles learned playing this game, as shader logs for its build: what earlier builds read off SD cards, where games left the
    list of what a console had to compile until 2026-11-15 (they use no card now). Kept with the game's other logs in the repository, in the
    work folder for the standalone app."""
    kept = os.path.join(ROOT, 'games', slug, 'shader_logs') if WORK == ROOT else os.path.join(WORK, 'learned', slug)
    return [os.path.join(kept, f) for f in sorted(os.listdir(kept)) if f.startswith('console_card_')] if os.path.isdir(kept) else []


def tool(name):
    """one of the repository's tool scripts, loaded by its path: the app carries them as files, not as modules"""
    import importlib.util
    spec = importlib.util.spec_from_file_location(name, os.path.join(ROOT, 'tools', name + '.py'))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def make_title(game_dir, out, info, art=None, icon_art=None, style='picture', released='', case_scan=False, pad_bezel=None, pad_art=None,
               boot_sound=None, disable_runtime_shaders=False):
    """the title every game gets (tools/make_game_title.py), then the update's and DLC's files beside the game's"""
    tool('make_game_title').make_title(os.path.join(out, 'title'), game_dir, os.path.join(out, info['slug'] + '.rpx'), os.path.join(out, 'shaders'),
                                       info['title_id'], info['name'], info['product'], ARCHIVES, art, icon_art, style, released, case_scan, pad_bezel,
                                       pad_art, boot_sound, info.get('publisher'), disable_runtime_shaders)
    content = os.path.join(out, 'title', 'content')
    for f in os.listdir(game_dir):
        if f.startswith(('update_', 'dlc_')):
            os.link(os.path.join(game_dir, f), os.path.join(content, f))


def copy_to(source, target_dir):
    """the WUP folder into target_dir, compared file by file; what was there is replaced only once the new copy is right"""
    name = os.path.basename(source)
    target = os.path.join(target_dir, name)
    new = target + '_new'
    shutil.rmtree(new, ignore_errors=True)
    os.makedirs(new)
    names = sorted(os.listdir(source))
    total, done = sum(os.path.getsize(os.path.join(source, n)) for n in names), 0
    for n in names:
        with open(os.path.join(source, n), 'rb') as i, open(os.path.join(new, n), 'wb') as o:
            while chunk := i.read(8 << 20):
                o.write(chunk)
                done += len(chunk)
                PROGRESS.at(done / total * 0.8)
            o.flush()
            os.fsync(o.fileno())
    if any(filecmp.cmpfiles(source, new, names, shallow=False)[1:]):
        shutil.rmtree(new, ignore_errors=True)
        fail('La copia no coincide con el original: es posible que la tarjeta esté fallando. No se reemplazó ningún archivo.')
    if os.path.isdir(target):
        shutil.rmtree(target)
    os.rename(new, target)
    return target


PROGRESS = Progress()


class ShowCredits(argparse.Action):
    """--credits: who the builder is built on, then exit, before the required options are asked for (as --version does)"""

    def __call__(self, parser, namespace, values, option_string=None):
        print(credits.text())
        parser.exit()


MK7_TITLE_IDS = {0x0004000000030600, 0x0004000000030700, 0x0004000000030800, 0x0004000000030A00, 0x0004000000083800}
TRIFORCE_TITLE_IDS = {0x0004000000176F00, 0x0004000000177000, 0x0004000000177100}
PRETENDO_ONLINE_TITLE_IDS = MK7_TITLE_IDS | TRIFORCE_TITLE_IDS


def allocate_mk7_friend_code(work_dir):
    """Generate a random 3DS Principal ID + Friend Code that has never been used in any prior build."""
    import hashlib, secrets, struct
    reg_path = os.path.join(work_dir, 'mk7_friend_codes.json')
    try:
        with open(reg_path, encoding='utf-8') as f:
            reg = json.load(f)
    except (OSError, ValueError):
        reg = {}
    used_pids = set(reg.get('used_pids', []))
    used_fcs = set(reg.get('used_fcs', []))
    while True:
        pid = 0x40000000 + secrets.randbelow(0x3FFFFFFE)
        if pid in used_pids:
            continue
        chk = hashlib.sha1(struct.pack('<I', pid)).digest()[0] >> 1
        fc = (chk << 32) | pid
        if fc in used_fcs:
            continue
        break
    used_pids.add(pid)
    used_fcs.add(fc)
    os.makedirs(work_dir, exist_ok=True)
    with open(reg_path, 'w', encoding='utf-8') as f:
        json.dump({'used_pids': sorted(used_pids), 'used_fcs': sorted(used_fcs)}, f, indent=2)
    return pid, fc


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--version', action='version', version=f'{credits.APP} {credits.VERSION} ({credits.LICENCE}, {credits.SOURCE})')
    ap.add_argument('--credits', nargs=0, action=ShowCredits, help='the projects the builder is built on, and their licences')
    ap.add_argument('--game', required=True)
    ap.add_argument('--update')
    ap.add_argument('--dlc')
    ap.add_argument('--mod', action='append', default=[], help="a mod in Luma's layout (luma/titles/<title id>/: romfs, code.ips/.bps/.bin); several go on in order")
    ap.add_argument('--keys', help='boot9.bin and seeddb.bin from your own 3DS (default ~/.3ds)')
    ap.add_argument('--emulator', help='a 3DS emulator\'s user folder (Azahar, Citra) or the 0004009b folder GodMode9 copies off your 3DS '
                                         '(title, in CTRNAND), for the system archives')
    ap.add_argument('--otp', required=True)
    ap.add_argument('--card', help='an SD card: the WUP goes in its install folder')
    ap.add_argument('--out', help='a folder to put the WUP in instead')
    ap.add_argument('--keep', action='store_true', help='keep the work files, for a quicker rebuild')
    ap.add_argument('--art', help="a picture (the cover art, say) for the start-up screens and the Wii U Menu icon; the game's own icon otherwise")
    ap.add_argument('--name', help='the title shown in the Wii U Menu and the 3DS information screen')
    ap.add_argument('--publisher', help='the publisher shown on the 3DS information screen')
    ap.add_argument('--icon-art', help='a square picture for the Wii U Menu icon alone, where --art is a wide banner')
    ap.add_argument('--pad-bezel', help="a picture (854x480, say) the GamePad shows where it shows no screen, a bezel or overlay; black otherwise")
    ap.add_argument('--pad-art', help="a picture of its own for the GamePad's start-up screen, shown as it is; the TV's otherwise")
    ap.add_argument('--boot-sound', help="the game's own start-up sound, a .btsnd (tools/make_boot_sound.py); the one for every game otherwise")
    ap.add_argument('--save', help="a 3DS save to pack in, which the game takes on the console (saves.py): Checkpoint's, JKSM's or an emulator's folder")
    ap.add_argument('--no-menu', action='store_true', help='leave out the in-game options menu')
    ap.add_argument('--art-style', choices=('picture', 'console'), default='console',
                    help='the start-up screens: laid out as the Virtual Console shows a game, or --art as it is')
    ap.add_argument('--tdb', help="a folder for GameTDB's list of 3DS games, where the Virtual Console's screens find the year a game came out")
    ap.add_argument('--mixed', action='store_true', help='trial: fast code with only the video decoder in safe mode (build3ds.Game.mixed); a game '
                                                         'compat.json marks "mixed" uses this policy for base code; updates/code mods require --mixed')
    ap.add_argument('--mk7-online', action='store_true', help='exclusive Mario Kart 7 Pretendo 3DS online mode with unique Friend Code per build')
    ap.add_argument('--cold', action='store_true', help="for the checks: build it as a game nobody has built before, with none of this PC's lists of "
                                                        'its code and shaders (tools/check_ports.py --cold)')
    ap.add_argument('--jobs', type=int, default=max(1, (os.cpu_count() or 2) // 2))
    a = ap.parse_args()
    for s in (sys.stdout, sys.stderr):  # run from a console, whose code page may lack a character of a game's name: shown escaped, not fatal
        if s:
            s.reconfigure(errors='backslashreplace')
    if sys.platform != 'win32':
        fail('La compilación en Linux todavía no está disponible: esta versión compila en Windows.')
    check_compiler_policy()
    if not (a.card or a.out):
        fail('Selecciona una tarjeta SD o una carpeta para el WUP.')
    problem = user_files.check_otp(a.otp)
    if problem:
        fail(problem)

    PROGRESS.stage('Leyendo el juego')
    decrypt.use_keys(a.keys)
    try:
        game = dumps.Dump(a.game)
        if game.kind not in ('game', 'demo'):
            fail(f'Ese archivo es {"una actualización" if game.kind == "update" else "DLC" if game.kind == "dlc" else "no es un juego"}. Primero selecciona el juego.')
        extras = []
        for path, kind in ((a.update, 'update'), (a.dlc, 'dlc')):
            if path:
                d = dumps.Dump(path)
                if d.kind != kind or d.unique != game.unique:
                    fail(f'{os.path.basename(path)} no corresponde a la {"actualización" if kind == "update" else "DLC"} de este juego.')
                extras.append(d)
        encrypted = [d for d in [game] + extras if d.encrypted()]
        if encrypted and not decrypt.have_keys(a.keys):
            fail(f'{os.path.basename(encrypted[0].path)} está cifrado. Coloca boot9.bin y seeddb.bin de tu propia 3DS en {a.keys or decrypt.KEYS_DEFAULT}.')
        problem = user_files.check_key_folder(a.keys or decrypt.KEYS_DEFAULT) if encrypted else None
        if problem:
            fail(problem)
        game.read_names()
        mod_list = [mods.Mod(p, game.title_id) for p in a.mod]
    except (dumps.DumpError, decrypt.DecryptError, mods.ModError) as e:
        fail(str(e))
    update = next((d for d in extras if d.kind == 'update'), None)
    dlc = next((d for d in extras if d.kind == 'dlc'), None)
    profile = profile_for(game)
    known = None if a.cold else profile
    code_mods = any(m.changes_code() for m in mod_list)
    info = dict(known or derived(game))
    if a.name and a.name.strip():
        info['name'] = ' '.join(a.name.split())
    if a.publisher is not None:
        info['publisher'] = ' '.join(a.publisher.split())
    if update:
        info['slug'] += f'_v{update.version}'
    if a.cold:
        info['slug'] += '_cold'  # work folders of its own, and no list of entries under that name
    print(f'{info["name"]} ({game.title_id:016X}): {info["status"]}' + (f', update v{update.version}' if update else '') + (', with DLC' if dlc else '')
          + (f', {len(mod_list)} mod{"s" * (len(mod_list) > 1)}' if mod_list else ''), flush=True)

    game_dir = os.path.join(WORK_GAMES, info['slug'])
    tmp = game_dir + '_decrypting'  # plain copies of what came encrypted, gone once unpacked
    need = sum(d.size * (4 if d is game else 3) for d in [game] + extras) + (3 << 30)
    os.makedirs(WORK_GAMES, exist_ok=True)
    if shutil.disk_usage(WORK_GAMES).free < need:
        fail(f'Espacio libre insuficiente: esta compilación necesita aproximadamente {need >> 30} GB en la unidad donde está {WORK}.')

    PROGRESS.stage('Descifrando')
    for d in (game_dir, tmp):
        shutil.rmtree(d, ignore_errors=True)
        os.makedirs(d)
    try:
        plain = {d: dumps.Dump(decrypt.to_plain(d.path, os.path.join(tmp, d.kind), d.kind)) if d.encrypted() and d.kind != 'dlc' else d for d in [game] + extras}
        PROGRESS.at(0.8)
        sources = {}
        if dlc:
            sources = decrypt.dlc_contents(dlc.path, tmp) if dlc.encrypted() else dumps.plain_dlc_sources(dlc, tmp)

        PROGRESS.stage('Extrayendo archivos')
        dumps.extract_program(plain[game], game_dir, lambda d, t: PROGRESS.at(d / t * (0.6 if extras else 1)))
        if update:
            info_u = dumps.add_update(update, plain[update], game_dir, lambda d, t: PROGRESS.at(0.6 + d / t * 0.2))
            print(f'update v{update.version}: its code runs as {info_u["program_id"]}', flush=True)
        if dlc:
            print(f'DLC: {dumps.add_dlc(dlc, sources, game_dir, lambda d, t: PROGRESS.at(0.8 + d / t * 0.2))} contents', flush=True)
        archives = ARCHIVES
        os.makedirs(archives, exist_ok=True)  # the title takes whatever is in it, even if that's nothing
        if len(os.listdir(archives)) < len(dumps.SYSTEM_ARCHIVES) and a.emulator:
            got = dumps.import_system_archives(a.emulator, archives, tmp)
            print(f'system archives from your emulator\'s files: {len(got)}', flush=True)
        # mods go on before anything's recompiled, so their code is recompiled with the game's
        mods.apply(mod_list, game_dir, lambda s: print(s, flush=True))
        for m in mod_list:
            m.close()
    except (dumps.DumpError, decrypt.DecryptError, mods.ModError) as e:
        fail(str(e))
    for d in plain.values():
        d.f.close()
    shutil.rmtree(tmp, ignore_errors=True)
    if not dumps.have_system_archives(archives):  # a Miitopia built without them stopped on the console with FS "not found" (Nov 9)
        fail('Faltan los archivos del sistema de 3DS: los datos Mii, sin los cuales se detienen los juegos que los utilizan, y la fuente compartida. Indica con --emulator la carpeta de usuario de Azahar '
             ' (después de ejecutar File > Set Up System Files) o la carpeta 0004009b que GodMode9 copia desde tu 3DS (title, en CTRNAND).')
    with open(os.path.join(game_dir, 'code.bin'), 'rb') as f:  # and what this game's own code names of them (dumps.archives_named)
        lacking = [t for t in dumps.archives_named(f.read()) if not os.path.isfile(os.path.join(archives, t + '.romfs'))]
    if lacking:
        t = lacking[0]
        fail(f'Este juego lee {dumps.ARCHIVE_NAMES.get(t, "una fuente")} desde la propia 3DS, pero tus archivos del sistema no lo contienen: el juego '
             f'se detendría al iniciarse. Está en la carpeta {t[:8]}: con GodMode9, copia también esa carpeta desde tu 3DS ([1:] SYSNAND CTRNAND > title), '
             f'junto a 0004009b y vuelve a seleccionar la carpeta en «Archivos del sistema de 3DS». Azahar también la obtiene mediante File > Set Up System Files.')
    # what earlier runs of a game learned (render states it uses; code only static analysis misses) goes with it, if the code is the same.
    # An untested game's live under its own slug (games/g_<code>/shader_logs): a run's shader_misses.bin put there is compiled ahead next time.
    logs = os.path.join(ROOT, 'games', (known or derived(game))['slug'], 'shader_logs')
    if os.path.isdir(logs) and not a.cold and not code_mods:
        os.makedirs(os.path.join(game_dir, 'shader_logs'), exist_ok=True)
        for f in os.listdir(logs):
            if not f.startswith('other_') and os.path.isfile(os.path.join(logs, f)):
                shutil.copyfile(os.path.join(logs, f), os.path.join(game_dir, 'shader_logs', f))
    for f in [] if a.cold or code_mods else console_learned((known or derived(game))['slug']):
        os.makedirs(os.path.join(game_dir, 'shader_logs'), exist_ok=True)
        shutil.copyfile(f, os.path.join(game_dir, 'shader_logs', os.path.basename(f)))

    # after the dumps, as it sets the toolchain up as it loads; by path, so the standalone app uses the repository's copy and its paths
    import importlib.util
    spec = importlib.util.spec_from_file_location('build3ds', os.path.join(ROOT, 'tools', 'win', 'build3ds.py'))
    b3 = importlib.util.module_from_spec(spec)
    sys.modules['build3ds'] = b3
    spec.loader.exec_module(b3)
    b3.WORK = os.path.join(WORK, 'build_win_builder').replace('\\', '/')
    prepare_logs = b3.prepare_logs

    def with_vertex_programs(g, shadergen):  # the standalone app's logs name vertex programs by fingerprint: this dump's own dvlb.bin has their code
        prepare_logs(g, shadergen)
        made, left = shaderlogs.restore(g.dir + '/shader_logs')
        if made or left:
            print(f'vertex records from earlier runs: {made}, and {left} whose program this dump hasn\'t', flush=True)
    b3.prepare_logs = with_vertex_programs
    if not os.path.exists(b3.BIN + 'gcc.exe'):
        fail('Las herramientas de compilación no están instaladas: consulta builder/README.md.')
    g = b3.Game(info['slug'], game_dir)  # the recipe every game gets, read from this game's own code (build3ds.py)
    if a.cold:
        g.twin = derived(game)['slug']  # its own folder in games/ is no other game's: nothing of it is borrowed
    # Cold checks exclude learned data, while testing the same compiler policy as a normal build.
    if a.mixed or (profile and profile.get('mixed') and not code_mods):
        g.mixed()
    if g.mode == 'safe':
        print('hand-written code that keeps values in any register across its calls (Mobiclip\'s video decoder): every register goes through memory', flush=True)
    if update or a.cold or code_mods:
        g.upstream_functions = None  # learned base-code entries do not describe changed code or a cold build
    if a.cold:
        g.pipelines = None
    out = os.path.join(game_dir, 'out_measure' if b3.MEASURE else 'out')  # a measuring build apart from the one for a console
    g.out, g.rpx, g.nm, g.shaders = out, os.path.join(out, info['slug'] + '.rpx'), os.path.join(out, 'game.nm'), os.path.join(out, 'shaders')
    os.makedirs(out, exist_ok=True)

    PROGRESS.stage('Buscando el código del juego')
    # What earlier runs reached stays, and the finders add to it: a tested game's list (its base code's alone), or an untested game's own
    # games/g_<code>[_v<update>]/functions.txt, the entries its runs found missing (Kirby: Triple Deluxe's first call at 0x232670, 2026-11-08)
    learned = os.path.join(ROOT, 'games', known['slug'] if known else info['slug'], 'functions.txt')
    have_learned = not a.cold and not code_mods and not (known and update) and os.path.isfile(learned) and os.path.getsize(learned) > 0
    if have_learned:
        shutil.copyfile(learned, g.extra_functions)
    have_upstream = g.upstream_functions and os.path.isfile(g.upstream_functions) and os.path.getsize(g.upstream_functions) > 0
    if known and not update and not code_mods and (have_learned or have_upstream):
        print('code entries: what this game\'s tested builds found', flush=True)
    else:
        env = dict(os.environ, RECOMP_CRO_BASE=b3.CRO_BASE)
        for tool in ('find_entries.py', 'cro_entries.py'):
            r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', tool), g.dir] + (['--write'] if tool == 'find_entries.py' else []), env=env,
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
            print(r.stdout.strip()[-300:], flush=True)
            if r.returncode:
                fail(f'Falló {tool}')

    extra_rt_flags, rt_tag = None, None
    if a.mk7_online and game.title_id in PRETENDO_ONLINE_TITLE_IDS:
        pid, fc = allocate_mk7_friend_code(WORK)
        fc_s = f'{fc:012d}'
        print(f'[mk7-online] Pretendo 3DS activo | PID=0x{pid:08X} | Friend Code={fc_s[:4]}-{fc_s[4:8]}-{fc_s[8:12]}', flush=True)
        extra_rt_flags = f'-DMK7_PRETENDO_ONLINE=1 -DMK7_PRINCIPAL_ID=0x{pid:08x}u -DMK7_FRIEND_CODE=0x{fc:016x}ull'
        rt_tag = f'mk7_{pid:08x}'
    args = types.SimpleNamespace(jobs=a.jobs, force=False, extra_flags=extra_rt_flags, only=None, opt=None, rt_tag=rt_tag)
    sys.stdout = held = Held(Watch(sys.__stdout__, PROGRESS), b3.context)
    shader_stop = []

    def shaders():  # they need none of the code, so they're made while it compiles (Zelda's took 21 of its 254 s)
        b3.context.tag = 'shaders'
        try:
            b3.build_shaders(g, args)
        except BaseException as e:  # build3ds stops with SystemExit; anything else keeps its traceback, shown with the stage's output
            if not isinstance(e, SystemExit):
                import traceback
                traceback.print_exc(file=held.held)
            shader_stop.append(e)

    alongside = threading.Thread(target=shaders, daemon=True)
    try:
        PROGRESS.stage('Recompilando')
        alongside.start()
        b3.build_code(g, args)
        PROGRESS.stage('Shaders')
        alongside.join()
        print(held.held.getvalue(), end='', flush=True)
        if shader_stop:
            raise shader_stop[0] if isinstance(shader_stop[0], SystemExit) else SystemExit(f'shaders: {shader_stop[0]!r}')
    except SystemExit as e:
        sys.stdout = sys.__stdout__
        fail(f'La compilación se detuvo: {e}')
    sys.stdout = sys.__stdout__
    learned, dvlb = os.path.join(ROOT, 'games', derived(game)['slug'], 'shader_logs'), os.path.join(game_dir, 'shader_logs', 'dvlb.bin')
    if not known and not a.cold and WORK == ROOT and os.path.isfile(dvlb):  # its vertex programs, where its runs' logs go: make_app.py fingerprints the
        os.makedirs(learned, exist_ok=True)  # shipped logs' vertex records by them (shaderlogs.py), and ships no dvlb.bin
        shutil.copyfile(dvlb, os.path.join(learned, 'dvlb.bin'))

    PROGRESS.stage('Creando el título')
    released, case_scan = '', False
    if a.art_style == 'console' and a.tdb:
        import covers  # only here: the one download a build may make, and only with the Virtual Console's screens chosen
        released = covers.released(game.product, a.tdb)
        case_scan = covers.is_case_scan(a.art, a.tdb)  # the folder is the covers' own
    make_title(game_dir, out, info, a.art, a.icon_art, a.art_style, released, case_scan, a.pad_bezel, a.pad_art, a.boot_sound)
    if a.save:
        import saves
        try:
            saves.pack(a.save, os.path.join(out, 'title', 'content'))
        except (saves.SaveError, OSError) as e:
            fail(f"No se pudo incluir la partida de 3DS: {e}")
    if a.no_menu:
        open(os.path.join(out, 'title', 'content', 'no_menu'), 'wb').close()

    if b3.MEASURE and not getattr(b3, 'MEASURE_CONSOLE', False):  # the checks' own kind of build, for Cemu (build3ds.py): never packed, so none can reach a console
        print('a measuring build: not packed', flush=True)
        PROGRESS.done, PROGRESS.share = 100, 0
        PROGRESS.at(1)
        print(f'@done {out}', flush=True)
        return

    PROGRESS.stage('Empaquetando el WUP')
    wup = os.path.join(a.out or game_dir, info['folder'])
    shutil.rmtree(wup, ignore_errors=True)
    r = subprocess.run([sys.executable, os.path.join(ROOT, 'tools', 'pack_game_wup.py'), a.otp, os.path.join(out, 'title'), wup], cwd=ROOT,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors='replace')
    print(r.stdout.strip()[-400:], flush=True)
    if r.returncode:  # a failed pack can leave a title.tmd behind: only the exit code counts
        shutil.rmtree(wup, ignore_errors=True)
        fail('Falló el empaquetado del WUP.')

    where = wup
    if a.card:
        PROGRESS.stage('Copiando')
        install = os.path.join(a.card.rstrip('\\/') + os.sep, 'install')
        os.makedirs(install, exist_ok=True)
        size = sum(os.path.getsize(os.path.join(wup, f)) for f in os.listdir(wup))
        if shutil.disk_usage(install).free < size + (64 << 20):
            fail(f'La tarjeta SD necesita {size >> 20} MB libres para este juego.')
        where = copy_to(wup, install)
        shutil.rmtree(wup, ignore_errors=True)
    if not a.keep:  # the runtime's objects stay: every game shares them; delete all temporary game build files
        shutil.rmtree(out, ignore_errors=True)
        shutil.rmtree(os.path.join(b3.WORK, f'ppc_{info["slug"]}'), ignore_errors=True)
        if a.out or a.card:
            shutil.rmtree(game_dir, ignore_errors=True)
    PROGRESS.done, PROGRESS.share = 100, 0
    PROGRESS.at(1)
    print(f'@done {where}', flush=True)


if __name__ == '__main__':
    main()
