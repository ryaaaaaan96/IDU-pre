"""使用文件模拟 Flash，验证跨启动参数和中断删除恢复。"""

import os
from pathlib import Path
import subprocess
import sys
import tempfile

exe = str(Path(sys.argv[1]).resolve())
with tempfile.TemporaryDirectory(prefix="dp-storage-") as directory:
    flash = Path(directory) / "flash.bin"
    for phase in (
        "exercise",
        "factory_absent",
        "initial_write_failure",
        "unknown",
        "layout_read_error",
        "layout_erase_error",
        "layout_write_error",
        "map_failure",
        "mapping_repeat",
        "periodic",
    ):
        if phase == "periodic" and "--periodic" not in sys.argv:
            continue
        subprocess.run(
            [exe, "-flash_in_ram"],
            env={**os.environ, "IDU_DP_PHASE": phase},
            check=True,
            timeout=30,
        )
    for phase in (
        "initial_seed",
        "initial_recover",
        "seed",
        "recover",
        "incompatible",
        "incompatible_recover",
        "seed",
        "out_of_range",
        "range_recover",
        "seed",
        "point_read_error",
        "recover",
        "fallback_write_failure",
        "clear_unregistered",
        "seed",
        "write_uncertain",
        "recover",
        "defaults_uncertain",
        "defaults_uncertain_recover",
        "seed",
        "delete",
        "recover_delete",
        "seed",
        "delete_metadata",
        "recover_delete",
        "seed",
        "factory",
        "factory_recover",
        "seed",
        "factory_null",
        "factory_recover",
        "seed",
        "factory_failure",
        "factory_recover",
        "seed",
        "factory_unregistered",
        "factory_recover",
        "seed",
        "factory_unregistered_failure",
        "factory_recover",
        "seed",
        "factory_all",
        "factory_all_recover",
        "seed",
        "factory_all_failure",
        "factory_all_recover",
        "map_commit",
        "map_commit_recover",
        "seed",
        "map_read_error",
        "recover",
        "corrupt_seed_zero",
        "corrupt_recover",
        "corrupt_seed_duplicate",
        "corrupt_recover",
        "corrupt_seed_state",
        "corrupt_recover",
        "corrupt_seed_version",
        "corrupt_recover",
        "corrupt_seed_length",
        "corrupt_recover",
    ):
        if (
            phase in ("initial_seed", "seed", "map_commit") or phase.startswith("corrupt_seed_")
        ) and flash.exists():
            flash.unlink()
        subprocess.run(
            [exe, f"-flash={flash}"],
            env={**os.environ, "IDU_DP_PHASE": phase},
            check=True,
            timeout=30,
        )
