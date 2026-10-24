#!/usr/bin/env bash
set -euo pipefail
workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then source .venv/bin/activate; fi
for variant in reset hold debug continue bad-uart; do
    arguments=()
    cases=()
    policy="$variant"
    case "$variant" in
        hold) arguments=(-DEXTRA_CONF_FILE=hold.conf);;
        debug) arguments=(-DEXTRA_CONF_FILE=../../conf/debug.conf);;
        continue) arguments=(-DEXTRA_CONF_FILE=continue.conf); cases=(led);;
        bad-uart) arguments=(-DEXTRA_DTC_OVERLAY_FILE=bad-uart.overlay); policy=reset; cases=(protocol-init);;
    esac
    directory="build/IDU-pre/tests/runtime-$variant"
    env ZEPHYR_TOOLCHAIN_VARIANT=host west build -p auto -b native_sim/native/64 \
        applications/IDU-pre/tests/runtime -d "$directory" -- "${arguments[@]}"
    python3 applications/IDU-pre/tests/runtime/run.py "$directory/zephyr/zephyr.exe" "$policy" "${cases[@]}"
    if [[ "$variant" == reset ]]; then
        python3 applications/IDU-pre/tests/runtime/run.py "$directory/zephyr/zephyr.exe" degraded storage-degraded
    fi
done
