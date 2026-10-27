#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then
    source .venv/bin/activate
fi

test_dir="build/IDU-pre/tests"
mkdir -p "$test_dir"
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/points -d "$test_dir/points"
timeout 30s "$test_dir/points/zephyr/zephyr.exe" -flash_in_ram

# 使用新进程内存验证持久状态，而非仅重新挂载 NVS。
flash_test_file="$(mktemp "$workspace_dir/$test_dir/reboot-flash.XXXXXX")"
trap 'rm -f -- "$flash_test_file"' EXIT
for phase in write recover verify; do
    env IDU_REBOOT_PHASE="$phase" timeout 30s "$test_dir/points/zephyr/zephyr.exe" \
        -flash="$flash_test_file" -test=points::test_parameters_reboot_probe
done

# 产品寄存器路由、持久化和有界端口恢复。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/forwarding -d "$test_dir/forwarding"
timeout 30s "$test_dir/forwarding/zephyr/zephyr.exe" -flash_in_ram
rm -f -- "$flash_test_file"
flash_test_file="$(mktemp "$workspace_dir/$test_dir/forwarding-reboot.XXXXXX")"
for phase in write verify; do
    env IDU_FORWARDING_PHASE="$phase" timeout 30s "$test_dir/forwarding/zephyr/zephyr.exe" \
        -flash="$flash_test_file" -test=forwarding::test_reboot_probe
done
python3 applications/IDU-pre/tests/test_mcuboot_image.py
python3 applications/IDU-pre/tests/test_build_configuration.py
python3 applications/IDU-pre/tests/test_debug_session.py

# 公共状态接口、真实期限，以及不返回的复位接口。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/task_watchdog -d "$test_dir/task_watchdog"
python3 applications/IDU-pre/tests/task_watchdog/run.py "$test_dir/task_watchdog/zephyr/zephyr.exe"

bash applications/IDU-pre/tests/runtime/run.sh

bash applications/IDU-pre/tests/iap/run.sh

# ADC 契约、模块化遥测、可复用模型和事件驱动轮询。
for suite in adc io vport modbus_support communication shared_rs485 simulator pipeline models collector collect_state collect_modbus transmit forwarding_devices transmit_port process_runtime dataprocess reference_dp reference_ports reference_config reference_services; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite"
    if [[ "$suite" == io ]]; then
        python3 applications/IDU-pre/tests/io/run.py "$test_dir/$suite/zephyr/zephyr.exe"
    elif [[ "$suite" == forwarding_devices ]]; then
        timeout 30s "$test_dir/$suite/zephyr/zephyr.exe" -flash_in_ram
        # 用旧的合并绑定持久化，再以独立转发条目启动。
        rm -f -- "$flash_test_file"
        flash_test_file="$(mktemp "$workspace_dir/$test_dir/binding-reboot.XXXXXX")"
        for phase in legacy_write verify; do
            env IDU_BINDING_PHASE="$phase" timeout 30s "$test_dir/$suite/zephyr/zephyr.exe" \
                -flash="$flash_test_file" -test=forwarding_devices::test_binding_migration_reboot_probe
        done
    else
        timeout 30s "$test_dir/$suite/zephyr/zephyr.exe"
    fi
done

# 参数存储不可用时，相同 CO/TR 装配仍可运行。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/collect_state -d "$test_dir/collect_state-storage-fault" \
    -- -DEXTRA_CONF_FILE=storage-fault.conf
timeout 30s "$test_dir/collect_state-storage-fault/zephyr/zephyr.exe" -flash_in_ram

# 行内默认值、范围和启动时选择的国家配置独立于 CO/TR。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/point_parameters -d "$test_dir/point_parameters"
timeout 30s "$test_dir/point_parameters/zephyr/zephyr.exe" -flash_in_ram
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/point_parameters -d "$test_dir/point_parameters-country" \
    -- -DEXTRA_CONF_FILE=country.conf
timeout 30s "$test_dir/point_parameters-country/zephyr/zephyr.exe" -flash_in_ram

# 仅回调的采集配置不编译 Zephyr 客户端或转发模块。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/collector -d "$test_dir/collector-no-adapter" \
    -- -DEXTRA_CONF_FILE=adapter-off.conf
timeout 30s "$test_dir/collector-no-adapter/zephyr/zephyr.exe"

# IAPv3 使用扩展 ADU，按配置容量验证 RAW/RTU 传输。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/communication -d "$test_dir/communication-large" \
    -- -DEXTRA_CONF_FILE=large.conf
timeout 30s "$test_dir/communication-large/zephyr/zephyr.exe"

# 参考参数策略、逐键宽度转换和延迟复位。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/reference_services -d "$test_dir/reference_services-params" \
    -- -DEXTRA_CONF_FILE=params.conf
timeout 30s "$test_dir/reference_services-params/zephyr/zephyr.exe" -flash_in_ram

# 禁用处理任务时，字节读取接口仍可使用。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/reference_dp -d "$test_dir/reference_dp_off" \
    -- -DEXTRA_CONF_FILE=process_off.conf
timeout 30s "$test_dir/reference_dp_off/zephyr/zephyr.exe"

# 处理模块中的存储和传输可分别选择启用。
for variant in params transport; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        applications/IDU-pre/tests/dataprocess -d "$test_dir/dataprocess-$variant" \
        -- -DEXTRA_CONF_FILE="$variant.conf"
    timeout 30s "$test_dir/dataprocess-$variant/zephyr/zephyr.exe" -flash_in_ram
done

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/pipeline -d "$test_dir/pipeline-no-adapter" \
    -- -DEXTRA_CONF_FILE=adapter-off.conf
timeout 30s "$test_dir/pipeline-no-adapter/zephyr/zephyr.exe"

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/pipeline -d "$test_dir/pipeline-basic" \
    -- -DEXTRA_CONF_FILE=process-off.conf
timeout 30s "$test_dir/pipeline-basic/zephyr/zephyr.exe"

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/acquisition -d "$test_dir/acquisition"
python3 applications/IDU-pre/tests/acquisition/run.py "$test_dir/acquisition/zephyr/zephyr.exe"

# 精确大小分配、失败清理和初始化重试。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/point_storage -d "$test_dir/point_storage"
timeout 30s "$test_dir/point_storage/zephyr/zephyr.exe"

# 跨文件点表登记、非持久命令和持久参数反馈。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/table_registry -d "$test_dir/table_registry"
timeout 30s "$test_dir/table_registry/zephyr/zephyr.exe" -flash_in_ram

# 使用新进程模拟固件增删键、调整键顺序及哈希冲突。
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/parameter_records -d "$test_dir/parameter_records"
rm -f -- "$flash_test_file"
flash_test_file="$(mktemp "$workspace_dir/$test_dir/parameter-records.XXXXXX")"
for phase in write add remove readd collision verify seed_error range_change type_change width_change obsolete; do
    env IDU_RECORD_PHASE="$phase" timeout 30s "$test_dir/parameter_records/zephyr/zephyr.exe" \
        -flash="$flash_test_file"
done
