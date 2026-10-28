"""使用真实 Arm GDB 和进程内远程目标验证 debug.sh。
需要已构建的应用 ELF 及其 SDK GDB，
也可指定 IDU_GDB；不使用探针。
"""

import os
from pathlib import Path
import re
import shutil
import socket
import struct
import subprocess
import tempfile
import threading
import unittest

APP = Path(__file__).resolve().parents[1]
WORKSPACE = APP.parents[1]
BUILD = WORKSPACE / "build/IDU-pre/mcuboot-iapv3/IDU-pre"


class RemoteTarget:
    def __init__(self, cpuid=0x410FD214):
        self.cpuid = cpuid
        self.pc = 0xDEADBEEF
        self.monitors = []
        self.writes = []
        self.errors = []
        self.socket = socket.socket()
        self.socket.bind(("127.0.0.1", 0))
        self.socket.listen(1)
        self.socket.settimeout(10)
        self.port = self.socket.getsockname()[1]
        self.thread = threading.Thread(target=self.serve, daemon=True)

    def registers(self):
        if self.pc == 0xDEADBEEF:
            return [self.pc] * 17
        return [0] * 13 + [0x20001000, 0x08001001, self.pc, 0x01000000]

    def respond(self, packet):
        if packet.startswith(b"qSupported"):
            return b"PacketSize=1000;qXfer:features:read+;qXfer:memory-map:read+"
        if packet.startswith(b"qXfer:"):
            parts = packet.split(b":")
            offset, size = (int(n, 16) for n in parts[-1].split(b","))
            if parts[1] == b"features":
                regs = "".join(
                    f'<reg name="{name}" bitsize="32" regnum="{i}"/>'
                    for i, name in enumerate([*(f"r{i}" for i in range(13)), "sp", "lr", "pc"])
                )
                data = (
                    '<?xml version="1.0"?><!DOCTYPE target SYSTEM "gdb-target.dtd">'
                    "<target><architecture>arm</architecture>"
                    '<feature name="org.gnu.gdb.arm.m-profile">'
                    + regs
                    + '<reg name="xpsr" bitsize="32" regnum="25"/></feature></target>'
                ).encode()
            else:
                data = (
                    b'<memory-map><memory type="flash" start="0x08000000" '
                    b'length="0x80000"><property name="blocksize">0x2000</property>'
                    b'</memory><memory type="ram" start="0xe0000000" '
                    b'length="0x100000"/></memory-map>'
                )
            return (b"l" if offset + size >= len(data) else b"m") + data[offset : offset + size]
        if packet == b"?":
            return b"S05"
        if packet == b"g":
            return struct.pack("<17I", *self.registers()).hex().encode()
        if packet.startswith(b"p"):
            index = int(packet[1:], 16)
            value = (
                self.registers()[16 if index == 25 else index] if index <= 16 or index == 25 else 0
            )
            return struct.pack("<I", value).hex().encode()
        if packet.startswith(b"m"):
            address, size = (int(n, 16) for n in packet[1:].split(b","))
            if address == 0xE000ED00:
                return (
                    b"E01" if self.cpuid is None else struct.pack("<I", self.cpuid).hex().encode()
                )
            return b"00" * size
        if packet.startswith(b"qRcmd,"):
            command = bytes.fromhex(packet.split(b",")[1].decode()).decode()
            self.monitors.append(command)
            if command == "halt":
                self.pc = 0x080103A0
            elif command == "reset":
                self.pc = 0x080020AC
            return b"OK"
        if packet.startswith((b"vFlash", b"M", b"X")):
            self.writes.append(packet)
            return b"OK"
        if packet == b"qC":
            return b"QC1"
        if packet == b"qfThreadInfo":
            return b"m1"
        if packet == b"qsThreadInfo":
            return b"l"
        if packet == b"qAttached":
            return b"1"
        if packet.startswith((b"H", b"T", b"P", b"qSymbol", b"D", b"k")):
            return b"OK"
        return b""

    def serve(self):
        try:
            connection, _ = self.socket.accept()
            with connection:
                connection.settimeout(10)
                while True:
                    char = connection.recv(1)
                    if not char:
                        break
                    if char != b"$":
                        continue
                    raw = bytearray()
                    while True:
                        char = connection.recv(1)
                        if not char:
                            raise RuntimeError("Truncated GDB packet")
                        if char == b"#":
                            break
                        raw.extend(char)
                    checksum = connection.recv(1) + connection.recv(1)
                    if int(checksum, 16) != sum(raw) & 255:
                        raise RuntimeError("Bad GDB checksum")
                    connection.sendall(b"+")
                    packet = bytearray()
                    iterator = iter(raw)
                    for byte in iterator:
                        packet.append(next(iterator) ^ 32 if byte == 125 else byte)
                    answer = self.respond(bytes(packet))
                    escaped = bytearray()
                    for byte in answer:
                        escaped.extend((125, byte ^ 32) if byte in b"$#}*" else (byte,))
                    connection.sendall(b"$" + escaped + b"#" + f"{sum(escaped) & 255:02x}".encode())
        except Exception as error:
            self.errors.append(error)
        finally:
            self.socket.close()


class DebugSessionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.elf = BUILD / "zephyr/IDU-pre.elf"
        cache = BUILD / "CMakeCache.txt"
        match = (
            re.search(r"^CMAKE_GDB:[^=]*=(.+)$", cache.read_text(), re.M)
            if cache.exists()
            else None
        )
        cls.gdb = os.environ.get("IDU_GDB") or (
            match.group(1) if match else shutil.which("arm-zephyr-eabi-gdb")
        )
        if not cls.elf.is_file() or not cls.gdb or not Path(cls.gdb).is_file():
            raise unittest.SkipTest("Build the Arm application first; SDK GDB and ELF are required")

    def run_session(self, target, attach=False, inspect=True):
        target.thread.start()
        directory = WORKSPACE / "build/IDU-pre/tests/debug-session"
        directory.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(dir=directory) as temporary:
            image = Path(temporary) / "fixture.hex"
            image.write_text(":020000040801F1\n:0400000001020304F2\n:00000001FF\n")
            command = [
                str(APP / "scripts/debug.sh"),
                "--host",
                "127.0.0.1",
                "--port",
                str(target.port),
                "--elf",
                str(self.elf),
                "--gdb",
                self.gdb,
            ]
            command += ["--attach"] if attach else ["--hex", str(image)]
            command += ["--", "-batch"]
            if inspect:
                command += ["-ex", "info registers pc sp xpsr"]
            result = subprocess.run(command, capture_output=True, text=True, timeout=15)
        target.thread.join(2)
        self.assertFalse(target.thread.is_alive(), "Remote session did not close")
        self.assertEqual(target.errors, [])
        return result

    def test_attach_refreshes_registers_after_monitor_halt(self):
        target = RemoteTarget()
        result = self.run_session(target, attach=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"pc\s+0x80103a0\b")
        self.assertEqual(target.monitors, ["halt"])
        self.assertEqual(target.writes, [])

    def test_download_refreshes_registers_after_final_reset(self):
        target = RemoteTarget()
        result = self.run_session(target)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertRegex(result.stdout, r"pc\s+0x80020ac\b")
        self.assertEqual(target.monitors, ["halt", "reset", "exec SetVerifyDownload = 1", "reset"])
        self.assertTrue(target.writes)

    def test_unreadable_cpuid_prevents_reset_and_download(self):
        target = RemoteTarget(cpuid=None)
        result = self.run_session(target, inspect=False)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(target.monitors, ["halt"])
        self.assertEqual(target.writes, [])

    def test_invalid_cpuid_prevents_reset_and_download(self):
        for cpuid in (0, 0xFFFFFFFF):
            with self.subTest(cpuid=cpuid):
                target = RemoteTarget(cpuid=cpuid)
                result = self.run_session(target, inspect=False)
                self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertEqual(target.monitors, ["halt"])
                self.assertEqual(target.writes, [])


if __name__ == "__main__":
    unittest.main()
