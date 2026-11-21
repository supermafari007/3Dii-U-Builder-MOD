"""Self-check for user_files: well-formed and broken stand-ins for otp.bin, boot9.bin and seeddb.bin (random or zero
bytes, never real keys), and bundled() finding them in a folder. Run: python builder/test_user_files.py"""
import os
import struct
import sys
import tempfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import user_files as uf  # noqa: E402


def put(folder, name, data):
    path = os.path.join(folder, name)
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'wb') as f:
        f.write(data)
    return path


with tempfile.TemporaryDirectory() as tmp:
    good = bytearray(1024)
    good[uf.OTP_COMMON_KEY] = os.urandom(16)
    assert uf.check_otp(put(tmp, 'otp.bin', bytes(good))) is None
    assert any(w in uf.check_otp(put(tmp, 'blank/otp.bin', bytes(1024))) for w in ('empty', 'vacío'))
    assert '1024' in uf.check_otp(put(tmp, 'short/otp.bin', bytes(1000)))
    assert any(w in uf.check_otp(os.path.join(tmp, 'nowhere', 'otp.bin')) for w in ('missing', 'Falta'))

    keys = os.path.join(tmp, 'keys')
    assert any(w in uf.check_key_folder(keys) for w in ('missing', 'Falta'))
    put(keys, 'boot9.bin', os.urandom(0x10000))
    assert uf.check_key_folder(keys) is None  # no seeddb.bin is fine
    assert uf.check_boot9(put(tmp, 'half/boot9.bin', os.urandom(0x8000))) is None
    assert 'boot ROM' in uf.check_boot9(put(tmp, 'odd/boot9.bin', os.urandom(1234)))
    put(keys, 'seeddb.bin', struct.pack('<I', 2) + bytes(12) + os.urandom(0x40))
    assert uf.check_key_folder(keys) is None
    put(keys, 'seeddb.bin', struct.pack('<I', 3) + bytes(12) + os.urandom(0x40))
    assert 'seed' in uf.check_key_folder(keys)

    clean = os.path.join(tmp, 'app')
    put(clean, 'engine/tools/readme.txt', b'x')
    assert uf.bundled(clean) == []
    put(clean, 'engine/deep/SeedDB.bin', b'x')
    assert [os.path.basename(p) for p in uf.bundled(clean)] == ['SeedDB.bin']
print('user files: ok')
