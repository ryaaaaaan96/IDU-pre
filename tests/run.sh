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
    -I applications/IDU-pre/app -I applications/IDU-pre/modules/io_input/include \
    applications/IDU-pre/tests/test_registers.c \
    applications/IDU-pre/tests/legacy_registers.c \
    applications/IDU-pre/app/process/engineering.c \
    -o "$test_dir/test_registers"
"$test_dir/test_registers"

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

# Full imported business profile uses its own native flash image.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/aidc -d "$test_dir/aidc"
timeout 30s "$test_dir/aidc/zephyr/zephyr.exe" -flash_in_ram

# Active MV-IO contracts, legacy partition isolation and bounded port recovery.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/mvio -d "$test_dir/mvio"
timeout 30s "$test_dir/mvio/zephyr/zephyr.exe" -flash_in_ram
rm -f -- "$flash_test_file"
flash_test_file="$(mktemp "$workspace_dir/$test_dir/mvio-reboot.XXXXXX")"
for phase in import verify; do
    env IDU_MVIO_PHASE="$phase" timeout 30s "$test_dir/mvio/zephyr/zephyr.exe" \
        -flash="$flash_test_file" -test=mvio::test_reboot_probe
done
python3 applications/IDU-pre/tests/test_boot_image.py
python3 applications/IDU-pre/tests/test_import_aidc.py
python3 applications/IDU-pre/tests/test_build_configuration.py

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/image_confirm -d "$test_dir/image_confirm"
timeout 30s "$test_dir/image_confirm/zephyr/zephyr.exe"

bash applications/IDU-pre/tests/iap/run.sh

# ADC contracts, modular telemetry, reusable models and event-driven polling.
for suite in adc io pipeline models collector transmit dataprocess reference_dp reference_ports reference_config reference_services; do
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        "applications/IDU-pre/tests/$suite" -d "$test_dir/$suite"
    timeout 30s "$test_dir/$suite/zephyr/zephyr.exe"
done

# Reference parameter policies, explicit schema migration and deferred reboot.
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
