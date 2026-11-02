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
timeout 30s "$test_dir/dp_core/zephyr/zephyr.exe" -flash_in_ram
# 产品模型与本地 process 装配。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/process_runtime -d "$test_dir/process_runtime"
timeout 30s "$test_dir/process_runtime/zephyr/zephyr.exe" -flash_in_ram

# 同一套行为分别验证线性查找和常驻 hashmap 索引。
for suite in dp_core process_runtime; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite-hash" -- \
        -DEXTRA_CONF_FILE=hash.conf
    timeout 30s "$test_dir/$suite-hash/zephyr/zephyr.exe" -flash_in_ram
done

# 真实 NVS 与文件模拟 Flash：跨启动恢复、写失败和删除中断。
for variant in normal hash three_slots; do
    extra=()
    if [[ "$variant" != normal ]]; then
        extra=(-- -DEXTRA_CONF_FILE="$variant.conf")
    fi
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        applications/IDU-pre/tests/dp_storage -d "$test_dir/dp_storage-$variant" "${extra[@]}"
    python3 applications/IDU-pre/tests/dp_storage/run.py \
        "$test_dir/dp_storage-$variant/zephyr/zephyr.exe"
done
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/dp_storage -d "$test_dir/dp_storage-periodic" -- \
    -DEXTRA_CONF_FILE=periodic.conf
env IDU_DP_PHASE=periodic timeout 30s \
    "$test_dir/dp_storage-periodic/zephyr/zephyr.exe" -flash_in_ram

for suite in adc modbus_support; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite"
    timeout 30s "$test_dir/$suite/zephyr/zephyr.exe"
done

# Modbus 模块独立 RAW 服务端，不链接 DP/transmit。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/modbus_support -d "$test_dir/modbus_support-server" -- \
    -DEXTRA_CONF_FILE=server.conf
timeout 40s "$test_dir/modbus_support-server/zephyr/zephyr.exe"

# Zephyr 4.5 专用队列：自有 RAW 仍同步，其他接口遵循官方队列配置。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/modbus_support -d "$test_dir/modbus_support-workqueue" -- \
    '-DEXTRA_CONF_FILE=server.conf;workqueue.conf'
timeout 40s "$test_dir/modbus_support-workqueue/zephyr/zephyr.exe"

# 完整字节/事件路径及产品 APP 串口线程。
for suite in transmit_stream protocol_port; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite"
    timeout 40s "$test_dir/$suite/zephyr/zephyr.exe" -flash_in_ram
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
