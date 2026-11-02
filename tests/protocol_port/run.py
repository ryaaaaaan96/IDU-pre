#!/usr/bin/env python3
"""使用同一模拟 Flash 跨启动验证 Modbus 保存与恢复默认命令。"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    executable = Path(sys.argv[1]).resolve()
    with tempfile.TemporaryDirectory(prefix="idu-protocol-") as directory:
        flash = Path(directory) / "flash.bin"
        for phase in ("write", "save", "defaults"):
            environment = os.environ.copy()
            environment.pop("IDU_PROTOCOL_VERIFY", None)
            if phase != "write":
                environment["IDU_PROTOCOL_VERIFY"] = phase
            subprocess.run(
                [str(executable), f"-flash={flash}"],
                env=environment,
                check=True,
                timeout=40,
            )


if __name__ == "__main__":
    main()
