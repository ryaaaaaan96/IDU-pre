"""Check factory HEX structure and IDU flash layout before full-chip programming.

This checks layout, not cryptographic authenticity; build/export verifies signatures.
"""
import argparse
from pathlib import Path
import struct


def check(path):
    memory = {}
    base = 0
    eof = False
    for number, line in enumerate(path.read_text(encoding="ascii").splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        if eof or not line.startswith(":"):
            raise ValueError(f"invalid record at line {number}")
        record = bytes.fromhex(line[1:])
        if len(record) < 5 or len(record) != record[0] + 5 or sum(record) & 255:
            raise ValueError(f"invalid length/checksum at line {number}")
        size, address, kind = record[0], int.from_bytes(record[1:3], "big"), record[3]
        data = record[4:-1]
        if kind == 0:
            for offset, value in enumerate(data):
                location = base + address + offset
                if not 0x08000000 <= location < 0x08080000:
                    raise ValueError("HEX data outside IDU internal flash")
                if location in memory and memory[location] != value:
                    raise ValueError("conflicting HEX records")
                memory[location] = value
        elif kind == 1 and size == 0 and address == 0:
            eof = True
        elif kind in (2, 4) and size == 2 and address == 0:
            base = int.from_bytes(data, "big") << (4 if kind == 2 else 16)
        elif kind in (3, 5) and size == 4 and address == 0:
            pass
        else:
            raise ValueError(f"unsupported/malformed record at line {number}")
    if not eof:
        raise ValueError("missing HEX end-of-file record")

    def read(address, size):
        try:
            return bytes(memory[address + offset] for offset in range(size))
        except KeyError as error:
            raise ValueError(f"missing factory image data at {error.args[0]:#x}") from error

    magic, _, header, _, image_size, flags = struct.unpack("<IIHHII", read(0x08010000, 20))
    if magic != 0x96F3B83D or header != 0x200 or flags or not 0 < image_size <= 0x6DE00:
        raise ValueError("invalid MCUboot application header/layout")
    read(0x08010000 + header, image_size)
    for address, start, end in ((0x08000000, 0x08000000, 0x08010000),
                                (0x08010200, 0x08010200, 0x08010200 + image_size)):
        stack, reset = struct.unpack("<II", read(address, 8))
        entry = reset & ~1
        if (not 0x20000000 < stack <= 0x20020000 or stack % 8 or
                not reset & 1 or not start <= entry < end):
            raise ValueError(f"invalid boot/application vectors at {address:#x}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("hex", type=Path)
    args = parser.parse_args()
    try:
        check(args.hex)
    except (OSError, UnicodeError, ValueError) as error:
        parser.error(str(error))
    print("Factory HEX checksum, address bounds and boot/application layout: OK")


if __name__ == "__main__":
    main()
