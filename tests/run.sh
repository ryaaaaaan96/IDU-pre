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
    -I applications/IDU-pre/include \
    applications/IDU-pre/tests/test_registers.c \
    applications/IDU-pre/tests/legacy_registers.c \
    applications/IDU-pre/src/registers.c \
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

# Active MV-IO firmware contracts, legacy flash import and bounded port recovery.
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

env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/boot -d "$test_dir/boot"
timeout 30s "$test_dir/boot/zephyr/zephyr.exe"

bash applications/IDU-pre/tests/iap/run.sh
