import importlib.util
from pathlib import Path
import struct
import unittest

spec = importlib.util.spec_from_file_location("boot_image", Path(__file__).resolve().parents[1] / "tools/check_boot_image.py")
checker = importlib.util.module_from_spec(spec)
spec.loader.exec_module(checker)


def record(kind, address, payload=b""):
    data = bytes([len(payload), address >> 8, address & 255, kind]) + payload
    return ":" + (data + bytes([-sum(data) & 255])).hex() + "\n"


class BootImageTest(unittest.TestCase):
    def setUp(self):
        self.image = struct.pack("<II", 0x20020000, 0x08004009) + b"\x00\xbf\x00\xbf"

    def ihex(self, image=None, start=0x4000):
        return record(4, 0, b"\x08\x00") + record(0, start, self.image if image is None else image) + record(1, 0)

    def test_vectors_and_matching_outputs(self):
        result = checker.validate(self.image, self.ihex())
        self.assertTrue(result["static_layout_valid"])
        self.assertFalse(result["hardware_verified"])

    def test_invalid_vectors(self):
        for sp, reset in [(0x20070000, 0x08004009), (0x20000101, 0x08004009),
                          (0x20001000, 0x08004008), (0x20001000, 0x08000001)]:
            image = struct.pack("<II", sp, reset) + self.image[8:]
            with self.subTest(sp=sp, reset=reset), self.assertRaises(ValueError):
                checker.validate(image, self.ihex(image))

    def test_boot_overlap_mismatch_and_truncation(self):
        for text in [self.ihex(start=0), self.ihex(self.image[:-1] + b"\xff"),
                     self.ihex()[:-12], self.ihex() + record(0, 0x4000, self.image)]:
            with self.subTest(hex=text), self.assertRaises(ValueError):
                checker.validate(self.image, text)

    def test_bad_checksum_and_oversized_binary(self):
        with self.assertRaises(ValueError):
            checker.validate(self.image, self.ihex().replace(":00000001ff", ":00000001fe"))
        with self.assertRaises(ValueError):
            checker.validate(self.image + bytes(496 * 1024), self.ihex())


if __name__ == "__main__":
    unittest.main()
