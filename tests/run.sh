#!/usr/bin/env bash
set -euo pipefail

workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../.." && pwd)"
cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then
    source .venv/bin/activate
fi

test_dir="build/IDU-pre/tests"
mkdir -p "$test_dir"
gcc -std=c11 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -I applications/IDU-pre/app \
    applications/IDU-pre/tests/test_engineering.c \
    applications/IDU-pre/app/process/engineering.c \
    -o "$test_dir/test_engineering"
"$test_dir/test_engineering"

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/points -d "$test_dir/points"
timeout 30s "$test_dir/points/zephyr/zephyr.exe" -flash_in_ram

# Verify durable state with fresh process memory, not just a remounted NVS.
flash_test_file="$(mktemp "$workspace_dir/$test_dir/reboot-flash.XXXXXX")"
trap 'rm -f -- "$flash_test_file"' EXIT
for phase in write recover verify; do
    env IDU_REBOOT_PHASE="$phase" timeout 30s "$test_dir/points/zephyr/zephyr.exe" \
        -flash="$flash_test_file" -test=points::test_parameters_reboot_probe
done

# Product register routing, persistence and bounded port recovery.
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

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/image_confirm -d "$test_dir/image_confirm"
timeout 30s "$test_dir/image_confirm/zephyr/zephyr.exe"

# Public status API, real deadlines and non-returning reset boundary.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/task_watchdog -d "$test_dir/task_watchdog"
python3 applications/IDU-pre/tests/task_watchdog/run.py "$test_dir/task_watchdog/zephyr/zephyr.exe"

bash applications/IDU-pre/tests/runtime/run.sh

bash applications/IDU-pre/tests/iap/run.sh

# ADC contracts, modular telemetry, reusable models and event-driven polling.
for suite in adc io vport modbus_support communication shared_rs485 simulator pipeline models collector collect_state collect_modbus transmit forwarding_devices transmit_port dataprocess reference_dp reference_ports reference_config reference_services; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite"
    if [[ "$suite" == forwarding_devices ]]; then
        timeout 30s "$test_dir/$suite/zephyr/zephyr.exe" -flash_in_ram
        # Persist with the old combined binding; boot with separate forward entries.
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

# The same CO/TR assembly keeps working with unavailable parameter storage.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/collect_state -d "$test_dir/collect_state-storage-fault" \
    -- -DEXTRA_CONF_FILE=storage-fault.conf
timeout 30s "$test_dir/collect_state-storage-fault/zephyr/zephyr.exe" -flash_in_ram

# Inline defaults/ranges and a startup-selected country are independent of CO/TR.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/point_parameters -d "$test_dir/point_parameters"
timeout 30s "$test_dir/point_parameters/zephyr/zephyr.exe" -flash_in_ram
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/point_parameters -d "$test_dir/point_parameters-country" \
    -- -DEXTRA_CONF_FILE=country.conf
timeout 30s "$test_dir/point_parameters-country/zephyr/zephyr.exe" -flash_in_ram

# Callback-only acquisition does not compile the Zephyr client or transmit.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/collector -d "$test_dir/collector-no-adapter" \
    -- -DEXTRA_CONF_FILE=adapter-off.conf
timeout 30s "$test_dir/collector-no-adapter/zephyr/zephyr.exe"

# IAPv3 uses extended ADUs; exercise RAW/RTU delivery at its configured capacity.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/communication -d "$test_dir/communication-large" \
    -- -DEXTRA_CONF_FILE=large.conf
timeout 30s "$test_dir/communication-large/zephyr/zephyr.exe"

# Reference parameter policies, per-key width conversion and deferred reboot.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/reference_services -d "$test_dir/reference_services-params" \
    -- -DEXTRA_CONF_FILE=params.conf
timeout 30s "$test_dir/reference_services-params/zephyr/zephyr.exe" -flash_in_ram

# Byte getters remain available when the processing worker is disabled.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/reference_dp -d "$test_dir/reference_dp_off" \
    -- -DEXTRA_CONF_FILE=process_off.conf
timeout 30s "$test_dir/reference_dp_off/zephyr/zephyr.exe"

# Storage and transport are independently optional in the processing module.
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
timeout 30s "$test_dir/acquisition/zephyr/zephyr.exe"

# Exact-size allocation, failure cleanup and initialization retry.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/point_storage -d "$test_dir/point_storage"
timeout 30s "$test_dir/point_storage/zephyr/zephyr.exe"

# Cross-file table registration, volatile commands and persistent feedback.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/table_registry -d "$test_dir/table_registry"
timeout 30s "$test_dir/table_registry/zephyr/zephyr.exe" -flash_in_ram

# Fresh processes model firmware adding/reordering/removing keys and hash collisions.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/parameter_records -d "$test_dir/parameter_records"
rm -f -- "$flash_test_file"
flash_test_file="$(mktemp "$workspace_dir/$test_dir/parameter-records.XXXXXX")"
for phase in write add remove readd collision verify seed_error range_change type_change width_change obsolete; do
    env IDU_RECORD_PHASE="$phase" timeout 30s "$test_dir/parameter_records/zephyr/zephyr.exe" \
        -flash="$flash_test_file"
done
