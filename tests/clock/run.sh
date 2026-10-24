#!/usr/bin/env bash
set -euo pipefail
workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
app_dir="$workspace_dir/applications/IDU-pre"
cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then
    source .venv/bin/activate
fi
# 需要 Arm 工具链；仅编译，不烧录实板。
for variant in 120mhz mismatched-cpu overclocked-apb1; do
    test_build_dir="$workspace_dir/build/IDU-pre/clock-$variant"
    test_log="$workspace_dir/build/IDU-pre/tests/clock-$variant.log"
    mkdir -p "$(dirname -- "$test_log")"
    if west build -p auto -b idu_pre zephyr/samples/hello_world -d "$test_build_dir" -- \
        "-DBOARD_ROOT=$app_dir" "-DDTS_ROOT=$app_dir" \
        "-DEXTRA_ZEPHYR_MODULES=$app_dir/modules/gd32e505" \
        "-DDTC_OVERLAY_FILE=$app_dir/tests/clock/$variant.overlay" >"$test_log" 2>&1; then
        if [[ "$variant" != 120mhz ]]; then
            echo "Invalid clock configuration was accepted: $variant" >&2
            exit 1
        fi
    else
        if [[ "$variant" == 120mhz ]]; then
            cat "$test_log"
            exit 1
        fi
        case "$variant" in
            mismatched-cpu) expected="CPU frequency must match HXTAL / prediv * mul" ;;
            overclocked-apb1) expected="APB1 exceeds 90 MHz" ;;
        esac
        if ! rg -F -q "$expected" "$test_log"; then
            cat "$test_log"
            exit 1
        fi
    fi
    echo "Clock configuration check passed: $variant"
done
