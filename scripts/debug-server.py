#!/usr/bin/env python3
"""启动支持 Zephyr 线程识别的 Windows J-Link GDB Server。"""

import argparse
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys


# 目标已修正为 GD32E505VET6，J-Link V8.78 识别此完整名称。
DEFAULT_DEVICE = "GD32E505VET6"
DEFAULT_PORT = 2331
DEFAULT_SPEED_KHZ = 1000


def find_jlink():
    """查找原生 Windows 安装目录，不连接探针。"""
    server = shutil.which("JLinkGDBServerCL.exe")
    if server:
        return Path(server).parent
    if sys.platform == "win32":
        import winreg

        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, r"Software\SEGGER\J-Link") as key:
                directory, _ = winreg.QueryValueEx(key, "InstallPath")
            candidate = Path(os.path.expandvars(directory))
            if (candidate / "JLinkGDBServerCL.exe").is_file():
                return candidate
        except OSError:
            pass
    program_files = os.environ.get("ProgramFiles")
    if program_files:
        candidates = [
            path for path in (Path(program_files) / "SEGGER").glob("JLink*")
            if (path / "JLinkGDBServerCL.exe").is_file()
        ]
        if candidates:
            return max(candidates, key=lambda path: (
                tuple(int(part) for part in re.findall(r"\d+", path.name)), path.name
            ))
    return None


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        epilog="Default: remote access for NAT WSL; no firewall changes. "
               "Device names are not substituted; use --device if your J-Link "
               "installation needs a confirmed generic mapping. See docs/wsl-jlink.md.",
        formatter_class=argparse.ArgumentDefaultsHelpFormatter,
    )
    parser.add_argument("--device", default=DEFAULT_DEVICE, help="J-Link debug profile (target: GD32E505VET6)")
    parser.add_argument("--jlink-dir", type=Path, help="Installation directory (auto-detected)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT, help="GDB TCP port")
    parser.add_argument("--speed", type=int, default=DEFAULT_SPEED_KHZ, help="SWD clock in kHz")
    parser.add_argument("--serial", help="Select a specific USB J-Link")
    parser.add_argument("--localhost-only", action="store_true", help="For mirrored WSL")
    parser.add_argument("--no-rtos", action="store_true", help="Debug the core without the Zephyr thread plugin")
    parser.add_argument("--dry-run", action="store_true", help="Print command without accessing the probe")
    args = parser.parse_args()
    if not 1 <= args.port <= 65535:
        parser.error("--port must be between 1 and 65535")
    if not 1 <= args.speed <= 50000:
        parser.error("--speed must be between 1 and 50000 kHz")
    if sys.platform != "win32" and not args.dry_run:
        parser.error("Run this script with Windows Python (py), not WSL Python; WSL supports --dry-run only")
    device = args.device.strip()
    if not device:
        parser.error("Device name must not be empty")

    directory = args.jlink_dir.expanduser() if args.jlink_dir else find_jlink()
    if directory is None:
        parser.error("J-Link installation not found; supply --jlink-dir")
    server = directory / "JLinkGDBServerCL.exe"
    plugin = directory / "GDBServer" / "RTOSPlugin_Zephyr.dll"
    for required in ([server] if args.no_rtos else [server, plugin]):
        if not required.is_file():
            parser.error(f"Required file not found: {required}")

    command = [
        str(server), "-device", device, "-if", "SWD", "-endian", "little",
        "-speed", str(args.speed), "-port", str(args.port),
        "-LocalhostOnly", "1" if args.localhost_only else "0",
        "-noir", "-noreset", "-halt",
    ]
    if not args.no_rtos:
        command.extend(["-rtos", str(plugin)])
    if args.serial:
        command.extend(["-USB", args.serial])
    print(f"Zephyr GDB Server: TCP {args.port}, device {device}, SWD {args.speed} kHz", flush=True)
    if args.dry_run:
        print(subprocess.list2cmdline(command))
        return 0
    try:
        return subprocess.call(command)
    except OSError as error:
        parser.error(f"Cannot start J-Link GDB Server: {error}")


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        sys.exit(130)
