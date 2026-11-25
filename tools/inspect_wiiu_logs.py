#!/usr/bin/env python3
"""Diagnostico rapido (solo lectura) de logs y shader_misses.bin extraidos de una Wii U.

Uso:
    python tools/inspect_wiiu_logs.py [ruta_archivos_wii_u]

Por defecto revisa: D:\\Downloads\\archivos wii u
"""
import json
import os
import re
import struct
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_DIR = r'D:\Downloads\archivos wii u'


def load_title_names():
    names = {}
    compat = os.path.join(ROOT, 'builder', 'compat.json')
    if os.path.isfile(compat):
        with open(compat, 'r', encoding='utf-8') as f:
            data = json.load(f)
        for k, v in data.items():
            if k.startswith('_') or not isinstance(v, dict):
                continue
            tid = v.get('title_id', '').lower()
            if len(tid) == 16:
                names[tid[8:]] = v.get('name', k)
    return names


def count_shader_records(path):
    if not os.path.isfile(path):
        return 0, 0, 0, 0
    with open(path, 'rb') as f:
        d = f.read()
    p = fs = vs = gsx = 0
    while p + 8 <= len(d):
        tag = d[p:p + 4]
        if tag == b'VS  ':
            if p + 16 > len(d):
                break
            _, nc, ns = struct.unpack_from('<III', d, p + 4)
            p = p + 16 + (nc + ns) * 4 + 56
            vs += 1
        elif tag == b'GSX ':
            sz = struct.unpack_from('<I', d, p + 4)[0]
            p = p + 8 + sz
            gsx += 1
        else:
            q = p + 4 if tag == b'FS  ' else p
            if q + 4 > len(d):
                break
            sz = struct.unpack_from('<I', d, q)[0]
            p = q + 4 + sz
            fs += 1
    return len(d), fs, vs, gsx


def inspect_log(path):
    if not os.path.isfile(path):
        return {'status': 'SIN LOG', 'detail': '-'}
    with open(path, 'r', encoding='utf-8', errors='replace') as f:
        lines = f.read().splitlines()

    hangs = []
    crashes = []
    last_st = None
    for l in lines:
        m = re.search(r'\[hang\] no new drawing for (\d+) s', l)
        if m and int(m.group(1)) >= 5:
            hangs.append((int(m.group(1)), l.strip()))
        if any(tok in l for tok in ('FATAL', 'crashed', 'bad memory access')):
            crashes.append(l.strip())
        if '[st] ' in l:
            last_st = l.strip()

    if crashes:
        status = 'CRASH'
        detail = crashes[-1][:95]
    elif hangs:
        status = f'HANG ({hangs[-1][0]}s)'
        detail = last_st.split('[st] ', 1)[1][:95] if last_st and '[st] ' in last_st else hangs[-1][1][:95]
    else:
        status = 'OK / HOME'
        detail = last_st.split('[st] ', 1)[1][:95] if last_st and '[st] ' in last_st else f'{len(lines)} lineas'

    return {'status': status, 'detail': detail}


def main():
    base = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DIR
    if not os.path.isdir(base):
        print(f'Carpeta no encontrada: {base}')
        return 1

    names = load_title_names()
    dirs = sorted(d for d in os.listdir(base) if os.path.isdir(os.path.join(base, d)))
    if not dirs:
        print(f'No hay carpetas de titulos en {base}')
        return 0

    print(f'Escaneando: {base} ({len(dirs)} titulos)\n')
    print(f'{"ID":10} {"Juego":34} {"Estado":12} {"Misses (FS/VS/GSX)":20} Resumen')
    print('-' * 120)
    for d in dirs:
        common = os.path.join(base, d, 'user', 'common')
        name = names.get(d.lower(), d)[:34]
        info = inspect_log(os.path.join(common, 'log.txt'))
        nbytes, fs, vs, gsx = count_shader_records(os.path.join(common, 'shader_misses.bin'))
        misses = f'{fs}/{vs}/{gsx} ({nbytes // 1024}K)' if nbytes else '-'
        print(f'{d:10} {name:34} {info["status"]:12} {misses:20} {info["detail"]}')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
