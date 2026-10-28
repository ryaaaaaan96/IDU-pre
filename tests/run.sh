#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then
    source .venv/bin/activate
fi

test_dir="build/IDU-pre/tests"
mkdir -p "$test_dir"

# DP 最小核心：常量、局部写读、范围、命令点、回调及 RAM 快照。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/dp_core -d "$test_dir/dp_core"
timeout 30s "$test_dir/dp_core/zephyr/zephyr.exe"
# 产品模型与本地 process 装配。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/process_runtime -d "$test_dir/process_runtime"
timeout 30s "$test_dir/process_runtime/zephyr/zephyr.exe"

for suite in adc modbus_support; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite"
    timeout 30s "$test_dir/$suite/zephyr/zephyr.exe"
done

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/io -d "$test_dir/io"
python3 applications/IDU-pre/tests/io/run.py "$test_dir/io/zephyr/zephyr.exe"

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/task_watchdog -d "$test_dir/task_watchdog"
python3 applications/IDU-pre/tests/task_watchdog/run.py "$test_dir/task_watchdog/zephyr/zephyr.exe"

bash applications/IDU-pre/tests/iap/run.sh

python3 applications/IDU-pre/tests/test_mcuboot_image.py
python3 applications/IDU-pre/tests/test_build_configuration.py
python3 applications/IDU-pre/tests/test_debug_session.py
