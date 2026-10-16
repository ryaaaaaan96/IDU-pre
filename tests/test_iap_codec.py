#!/usr/bin/env python3
"""Real legacy-compressor interoperability and signed SGZ rejection tests."""
import ctypes as C
import importlib.util
import os
from pathlib import Path
import random
import struct
import subprocess
import sys
import unittest
import zlib
from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

APP = Path(__file__).resolve().parents[1]
ROOT = APP.parents[1]
BUILD = ROOT / "build/IDU-pre/tests/iap-codec"
TINY = ROOT / "bootloader/mcuboot/ext/tinycrypt/lib"
# Optional reference checkout for old-compressor interoperability only.
# The firmware, fuzzing and signed SGZ tests do not depend on the old project.
LEGACY = Path(os.environ["IDU_LEGACY_IAP_DIR"]).expanduser() if os.environ.get("IDU_LEGACY_IAP_DIR") else None


def module(name, file):
    spec = importlib.util.spec_from_file_location(name, file)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


packer = module("pack_sgz", APP / "tools/pack_sgz.py")
pubtool = module("iap_public_key", APP / "tools/iap_public_key.py")
READ = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_uint32, C.c_void_p, C.c_size_t)
WRITE = C.CFUNCTYPE(C.c_int, C.c_void_p, C.c_void_p, C.c_size_t)


def build():
    BUILD.mkdir(parents=True, exist_ok=True)
    subprocess.run([sys.executable, str(APP / "tests/iap/fixtures.py"), str(BUILD)], check=True)
    sources = [APP / "src/iap/sgz.c", APP / "src/iap/verify_rng.c"] + [TINY / f"source/{x}.c" for x in
        ("aes_encrypt", "aes_decrypt", "sha256", "utils", "ecc", "ecc_dsa")]
    flags = ["-std=c11", "-Wall", "-Wextra", "-Werror", "-I", str(APP / "include"),
             "-I", str(TINY / "include")]
    subprocess.run(["gcc", "-shared", "-fPIC", *flags, *map(str, sources),
                    "-o", str(BUILD / "codec.so")], check=True)
    subprocess.run(["gcc", "-O1", "-g", "-fno-omit-frame-pointer", "-fsanitize=address,undefined",
                    *flags, str(APP / "tests/iap_fuzz.c"), *map(str, sources),
                    "-o", str(BUILD / "fuzz")], check=True)
    subprocess.run([str(BUILD / "fuzz")], check=True)
    if LEGACY is not None:
        if not (LEGACY / "quicklz.c").is_file():
            raise RuntimeError("IDU_LEGACY_IAP_DIR must contain the reference quicklz.c")
        subprocess.run(["gcc", "-shared", "-fPIC", "-O2", "-I", str(LEGACY / "include"),
                        str(LEGACY / "quicklz.c"), "-o", str(BUILD / "legacy.so")], check=True)


class Codec(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lib = C.CDLL(str(BUILD / "codec.so"))
        cls.lib.idu_qlz_decode.argtypes = [C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t]
        cls.lib.idu_sgz_decrypt.argtypes = [C.c_void_p, C.c_size_t, C.c_uint32]
        cls.lib.idu_sgz_unpack.argtypes = [READ, C.c_void_p, C.c_uint32, WRITE,
                                          C.c_void_p, C.c_void_p, C.POINTER(C.c_uint32)]
        cls.lib.idu_image_verify.argtypes = [READ, C.c_void_p, C.c_uint32,
                                            C.c_void_p, C.c_void_p, C.c_void_p]
        cls.old = C.CDLL(str(BUILD / "legacy.so")) if LEGACY is not None else None
        if cls.old is not None:
            cls.old.qlz_compress.argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p]
            cls.old.qlz_compress.restype = C.c_size_t
            cls.old.qlz_get_setting.argtypes = [C.c_int]
        cls.key = Path(os.environ.get("IDU_IAP_KEY", ROOT / "bootloader/mcuboot/root-ec-p256.pem"))
        cls.image = Path(os.environ.get("IDU_IAP_IMAGE", BUILD / "fixture.signed.bin")).read_bytes()
        cls.public, cls.digest = pubtool.public_material(cls.key)

    def unpack(self, data):
        result = bytearray()
        @READ
        def read(ctx, off, dst, length):
            if off + length > len(data):
                return -5
            C.memmove(dst, data[off:off + length], length)
            return 0
        @WRITE
        def write(ctx, src, length):
            result.extend(C.string_at(src, length))
            return 0
        size = C.c_uint32()
        ret = self.lib.idu_sgz_unpack(read, None, len(data), write, None,
                                      C.create_string_buffer(4352), C.byref(size))
        return ret, bytes(result)

    def verify(self, data, public=None, digest=None):
        @READ
        def read(ctx, off, dst, length):
            if off + length > len(data):
                return -5
            C.memmove(dst, data[off:off + length], length)
            return 0
        return self.lib.idu_image_verify(read, None, len(data), public or self.public,
                                         digest or self.digest, C.create_string_buffer(32))

    def compress(self, data):
        if self.old is None:
            self.skipTest("set IDU_LEGACY_IAP_DIR to test against the old QuickLZ compressor")
        state = C.create_string_buffer(self.old.qlz_get_setting(1))
        out = C.create_string_buffer(len(data) + 400)
        n = self.old.qlz_compress(data, out, len(data), state)
        return out.raw[:n]

    def test_legacy_quicklz(self):
        rng = random.Random(505)
        compressed = 0
        for size in list(range(1, 220)) + [255, 256, 511, 1024, 2048] * 20:
            for data in (rng.randbytes(size), (b"IDU-pre-1234567" * 2048)[:size], b"\0" * size):
                frame = self.compress(data)
                compressed += frame[0] & 1
                out = C.create_string_buffer(2048)
                self.assertEqual(self.lib.idu_qlz_decode(frame, len(frame), out, 2048), len(data),
                                 (size, frame.hex()))
                self.assertEqual(out.raw[:size], data)
                for cut in (0, 1, 2, len(frame) - 1):
                    self.assertLess(self.lib.idu_qlz_decode(frame, cut, out, 2048), 0)
        self.assertGreater(compressed, 300)

    def test_aes_transport_packets(self):
        encrypted = packer.pack(self.image)
        plain = packer.pack(self.image, encrypt=False)
        for step in (16, 240, 1024):
            out = bytearray()
            for offset in range(0, len(encrypted), step):
                data = encrypted[offset:offset + step]
                buf = C.create_string_buffer(data, len(data))
                self.assertEqual(self.lib.idu_sgz_decrypt(buf, len(data), offset), 0)
                out.extend(buf.raw)
            self.assertEqual(out, plain)
        self.assertLess(self.lib.idu_sgz_decrypt(C.create_string_buffer(17), 17, 0), 0)

    def test_stored_sgz_roundtrip(self):
        ret, image = self.unpack(packer.pack(self.image, encrypt=False))
        self.assertEqual(ret, 0)
        self.assertEqual(image, self.image)
        self.assertEqual(self.verify(image), 0)

    def test_exported_sgz(self):
        filename = os.environ.get("IDU_IAP_SGZ")
        if not filename:
            self.skipTest("production artifact checked when IDU_IAP_SGZ is set")
        encrypted = Path(filename).read_bytes()
        buf = C.create_string_buffer(encrypted, len(encrypted))
        self.assertEqual(self.lib.idu_sgz_decrypt(buf, len(encrypted), 0), 0)
        ret, image = self.unpack(buf.raw)
        self.assertEqual(ret, 0)
        self.assertEqual(image, self.image)
        self.assertEqual(self.verify(image), 0)

    def test_compressed_sgz_roundtrip(self):
        plain = bytearray(packer.pack(self.image, encrypt=False)[:132])
        payload = bytearray()
        for off in range(0, len(self.image), 2048):
            frame = self.compress(self.image[off:off + 2048])
            payload += struct.pack("<H", len(frame)) + frame
        struct.pack_into("<II", plain, 120, len(payload), zlib.crc32(payload))
        struct.pack_into("<I", plain, 20, 132 + len(payload))
        plain += payload
        struct.pack_into("<I", plain, 16, zlib.crc32(plain[20:]))
        plain += b"\0" * (-len(plain) % 16)
        ret, image = self.unpack(bytes(plain))
        self.assertEqual(ret, 0)
        self.assertEqual(image, self.image)

    def test_sgz_corruption_and_boundaries(self):
        original = packer.pack(self.image, encrypt=False)
        for offset in (0, 16, 20, 24, 28, 32, 40, 44, 120, 124, 128, 134, len(original) - 17):
            bad = bytearray(original)
            bad[offset] ^= 0x80
            self.assertLess(self.unpack(bytes(bad))[0], 0, offset)
        for size in (0, 16, 128, len(original) - 16, len(original) - 1):
            self.assertLess(self.unpack(original[:size])[0], 0)
        self.assertLess(self.unpack(original + b"\0" * 16)[0], 0)
        for offset, value in ((28, 2), (32, 1), (40, 0x70000), (128, 0xffffffff)):
            bad = bytearray(original)
            struct.pack_into("<I", bad, offset, value)
            length = struct.unpack_from("<I", bad, 20)[0]
            struct.pack_into("<I", bad, 16, zlib.crc32(bad[20:length]))
            self.assertLess(self.unpack(bytes(bad))[0], 0)

    def test_signature_rejections(self):
        for offset in (0, 8, 12, 16, 512, 516, 1024, len(self.image) - 1):
            bad = bytearray(self.image)
            bad[offset] ^= 0x80
            self.assertLess(self.verify(bytes(bad)), 0, offset)
        for size in (0, 512, len(self.image) - 1):
            self.assertLess(self.verify(self.image[:size]), 0)
        self.assertLess(self.verify(self.image, digest=b"\0" * 32), 0)
        other = ec.generate_private_key(ec.SECP256R1()).public_key().public_bytes(
            serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)[1:]
        self.assertLess(self.verify(self.image, public=other), 0)


if __name__ == "__main__":
    build()
    unittest.main()
