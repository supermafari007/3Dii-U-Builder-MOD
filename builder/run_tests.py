#!/usr/bin/env python3
"""Ejecuta las pruebas unitarias del Builder y valida compat.json antes de empaquetar."""
import json
import os
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
TESTS = [
    'test_shaderlogs.py',
    'test_saves.py',
    'test_user_files.py',
    'test_system_archives.py',
    'test_covers.py',
    'test_updater.py',
]


def main():
    compat = os.path.join(HERE, 'compat.json')
    with open(compat, 'r', encoding='utf-8') as f:
        data = json.load(f)
    entries = {k: v for k, v in data.items() if not k.startswith('_')}
    for pid, info in entries.items():
        for req in ('slug', 'title_id', 'name', 'product', 'folder', 'status'):
            assert req in info and info[req], f'{pid}: falta {req}'
        assert info['status'] in ('Complete', 'Plays', 'Untested'), f'{pid}: status invalido {info["status"]}'
    print(f'compat.json: ok ({len(entries)} perfiles)')

    failed = []
    for t in TESTS:
        r = subprocess.run([sys.executable, os.path.join(HERE, t)], cwd=HERE)
        if r.returncode != 0:
            failed.append(t)
    if failed:
        print(f'\nFALLARON {len(failed)} pruebas: {", ".join(failed)}')
        return 1
    print(f'\nTodas las pruebas ({len(TESTS)}/{len(TESTS)}) pasaron correctamente.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
