#!/usr/bin/env bash
set -euo pipefail
workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then
    source .venv/bin/activate
fi
python3 applications/IDU-pre/tests/test_iap_codec.py
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/iap -d build/IDU-pre/tests/iap
executable=build/IDU-pre/tests/iap/zephyr/zephyr.exe
timeout 30s "$executable" -flash_in_ram
flash_test_file="$(mktemp "$workspace_dir/build/IDU-pre/tests/iap/reboot.XXXXXX")"
trap 'rm -f -- "$flash_test_file"' EXIT
for phase in receive recover; do
    env IDU_IAP_PHASE="$phase" timeout 30s "$executable" -flash="$flash_test_file" \
        -test=iap::test_reboot_journal
done
rm -f -- "$flash_test_file"
for phase in pending confirmed confirmed_again; do
    env IDU_IAP_PHASE="$phase" timeout 30s "$executable" -flash="$flash_test_file" \
        -test=iap::test_reboot_journal
done
rm -f -- "$flash_test_file"
for phase in pending rollback; do
    env IDU_IAP_PHASE="$phase" timeout 30s "$executable" -flash="$flash_test_file" \
        -test=iap::test_reboot_journal
done

# Same module with configured partitions, different memory/key/limits and no DP.
env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
    applications/IDU-pre/tests/iap -d build/IDU-pre/tests/iap-configured -- \
    -DEXTRA_CONF_FILE=configured.conf -DEXTRA_DTC_OVERLAY_FILE=configured.overlay
timeout 30s build/IDU-pre/tests/iap-configured/zephyr/zephyr.exe -flash_in_ram
