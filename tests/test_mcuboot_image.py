"""校验当前 MCUboot 导出布局和构建选择。"""

import importlib.util
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from intelhex import IntelHex

SPEC = importlib.util.spec_from_file_location(
    "build_mcuboot", Path(__file__).resolve().parents[1] / "tools/build_mcuboot.py"
)
builder = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(builder)


class ImageExport(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / "IDU-pre/zephyr"
        self.boot = self.root / "mcuboot/zephyr"
        self.app.mkdir(parents=True)
        self.boot.mkdir(parents=True)
        self.blob = bytearray(0x300)
        struct.pack_into("<IIHHII", self.blob, 0, 0x96F3B83D, 0, 0x200, 0, 0x100, 0)
        struct.pack_into("<II", self.blob, 0x200, 0x20020000, 0x08010209)
        (self.app / "IDU-pre.signed.bin").write_bytes(self.blob)
        boot = struct.pack("<II", 0x20020000, 0x08000009) + bytes(24)
        (self.boot / "zephyr.bin").write_bytes(boot)
        image = IntelHex()
        image.frombytes(boot, offset=0x08000000)
        image.write_hex_file(str(self.boot / "zephyr.hex"))

    def export(self):
        # 固件构建时使用真实 imgtool 执行密码学验证。
        with patch.object(builder, "run") as run:
            builder.export(self.root, self.root / "key.pem", self.root / "out")
            self.assertEqual(run.call_args_list[0].args[2], "verify")

    def test_canonical_signed_bin_and_factory_layout(self):
        self.export()
        application = IntelHex(str(self.root / "out/IDU-pre.hex"))
        factory = IntelHex(str(self.root / "out/IDU-pre-factory.hex"))
        self.assertEqual(application.minaddr(), 0x08010000)
        self.assertEqual(application.tobinstr(), bytes(self.blob))
        self.assertEqual(factory[0x08010000], self.blob[0])
        self.assertEqual(factory[0x08000004], 9)

    def test_wrong_link_address_is_rejected(self):
        struct.pack_into("<I", self.blob, 0x204, 0x08004009)
        (self.app / "IDU-pre.signed.bin").write_bytes(self.blob)
        with self.assertRaisesRegex(ValueError, "reset vector"):
            self.export()

    def test_invalid_stack_and_header_are_rejected(self):
        for offset, value, message in ((0x200, 0x20020004, "stack"), (0, 0, "header")):
            with self.subTest(offset=offset):
                blob = bytearray(self.blob)
                struct.pack_into("<I", blob, offset, value)
                (self.app / "IDU-pre.signed.bin").write_bytes(blob)
                with self.assertRaisesRegex(ValueError, message):
                    self.export()

    def test_boot_hex_binary_mismatch_is_rejected(self):
        image = IntelHex(str(self.boot / "zephyr.hex"))
        image[0x08000010] = 0x42
        image.write_hex_file(str(self.boot / "zephyr.hex"))
        with self.assertRaisesRegex(ValueError, "bytes differ"):
            self.export()

    def test_slot_trailer_space_is_reserved(self):
        (self.app / "IDU-pre.signed.bin").write_bytes(
            self.blob + bytes(builder.SLOT_SIZE - len(self.blob))
        )
        with self.assertRaisesRegex(ValueError, "slot payload"):
            self.export()

    def test_default_iap_and_explicit_alternatives(self):
        key = self.root / "key.pem"
        key.touch()
        for flags, suffix, iap in (([], "-iapv3", True), (["--no-upload"], "", False)):
            with (
                self.subTest(flags=flags),
                patch.object(builder, "run") as run,
                patch.object(builder, "export") as export,
                patch("sys.argv", ["build_mcuboot", "--key", str(key), *flags]),
            ):
                builder.main()
                self.assertIn("--sysbuild", run.call_args.args)
                self.assertEqual(export.call_args.args[0].name, "mcuboot" + suffix)
                self.assertEqual(export.call_args.args[3], iap)


if __name__ == "__main__":
    unittest.main()
