"""Self-check of the recompiler's reading of RVCT's Thumb switch through a helper (gen.py, switch8): a `blx` to
__ARM_common_switch8_thumb with the case table right after it is a jump to the cases, and the table isn't code. Synthetic code, so it
runs anywhere. Run: python tools/test_recomp_switch.py"""
import json
import os
import shutil
import struct
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, 'third_party', '3dsrecomp'))
from tools.recomp import gen, image  # noqa: E402
from tools.recomp.ins import JMP  # noqa: E402

BASE = 0x100000
# 100000 (Thumb): movs r3, r0; push {lr}; blx 100020; then the table at 100008: 2 cases, then offsets 2, 4 and 6 (the last for any
# index of 2 or more), in halfwords from 100009; 10000c: movs r0, #1; pop {pc}; 100010: movs r0, #2; pop {pc}; 100014: movs r0, #3;
# pop {pc}; then padding. 100020 (ARM): the helper, its index in r3.
thumb = struct.pack('<4H', 0x0003, 0xB500, 0xF000, 0xE80C) + bytes([2, 2, 4, 6]) + struct.pack('<8H', 0x2001, 0xBD00, 0x2002, 0xBD00,
                                                                                               0x2003, 0xBD00, 0, 0)
helper = struct.pack('<6I', 0xE55EC001, 0xE153000C, 0x27DE300C, 0x37DE3003, 0xE08EC083, 0xE12FFF1C)
code = thumb + bytes(0x20 - len(thumb)) + helper + bytes(0x40)

d = tempfile.mkdtemp(prefix='switch_test_')
try:
    with open(os.path.join(d, 'code.bin'), 'wb') as f:
        f.write(code)
    with open(os.path.join(d, 'info.json'), 'w') as f:
        json.dump({'text': {'addr': BASE, 'size': len(code)}, 'rodata': {'addr': BASE + 0x1000, 'size': 0},
                   'data': {'addr': BASE + 0x2000, 'size': 0}}, f)
    an = gen.Analyzer(image.Image(d), log=lambda *a: None)
    i = an.decode(0x100004, 1)
    assert i.kind == JMP and i.jt == [0x10000D, 0x100011, 0x100015], f'the call is a jump to the three cases: {i}'
    c = i.code[0]
    # as the helper: the offset of index r3, or of the last entry for an index of 2 or more, into r3; the target into ip; lr as the call left it
    for part in ('u32 b = 2u;', 'cf = a >= b;', 'r3 = RD8(0x100009u + (a < b ? a : b));', 'r12 = 0x100009u + (r3 << 1);', 'r14 = 0x100009u;', 'tpc = r12;'):
        assert part in c, f'the helper\'s effects: {part} in {c}'
    insts, ok = an.explore(0x100001)
    assert ok and {0x10000C, 0x100010, 0x100014} <= set(insts), 'every case is explored as code'
    assert 0x100008 not in insts and 0x10000A not in insts, 'the table is not decoded as code'
    out = gen.Emitter(an, d).emit_function(0x100001)
    for case, label in ((0x10000D, 0x10000C), (0x100011, 0x100010), (0x100015, 0x100014)):
        assert f'case 0x{case:x}u: goto L_{label:x};' in out and f'L_{label:x}:' in out, f'case {case:x} is a label of the function'
    # anything else at the target stays a call
    j = gen.Analyzer(image.Image(d), log=lambda *a: None)
    j.img.text.data = j.img.text.data[:0x24] + struct.pack('<I', 0xE1A00000) + j.img.text.data[0x28:]
    assert j.decode(0x100004, 1).kind != JMP, 'a call to another function stays a call'
    print('armcc switch: ok')
finally:
    shutil.rmtree(d, ignore_errors=True)
