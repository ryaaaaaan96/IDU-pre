#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace_dir="$(cd -- "$app_dir/../.." && pwd)"

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    echo "Usage: $0 [idu_pre] [west build options...]"
    echo "       $0 [idu_pre] --mcuboot [--iapv3 | --smp-uart] [options...]"
    exit 0
fi

board_target=idu_pre
if [[ $# -gt 0 && "$1" != -* ]]; then
    board_target="$1"
    shift
fi
if [[ "$board_target" != idu_pre ]]; then
    echo "IDU-pre supports the idu_pre board; got: $board_target" >&2
    exit 2
fi
if [[ ! -d "$workspace_dir/.west" || ! -f "$workspace_dir/zephyr/CMakeLists.txt" ]]; then
    echo "Place IDU-pre at <West workspace>/applications/IDU-pre and run west update first." >&2
    exit 1
fi

cd -- "$workspace_dir"
if [[ -z "${VIRTUAL_ENV:-}" && -f .venv/bin/activate ]]; then
    source .venv/bin/activate
fi

if [[ "${1:-}" == --mcuboot ]]; then
    shift
    exec python3 "$app_dir/tools/build_mcuboot.py" "$@"
fi

build_dir="build/IDU-pre/$board_target"
west build -p auto -b "$board_target" "$app_dir" -d "$build_dir" "$@"

hex_source="$build_dir/zephyr/IDU-pre.hex"
if [[ ! -f "$hex_source" ]]; then
    echo "HEX output missing: $hex_source. Enable CONFIG_BUILD_OUTPUT_HEX and set CONFIG_KERNEL_BIN_NAME to IDU-pre." >&2
    exit 1
fi
python3 "$app_dir/tools/check_boot_image.py" \
    --bin "$build_dir/zephyr/IDU-pre.bin" --hex "$hex_source" \
    --output "$build_dir/zephyr/IDU-pre-boot-check.json"
mkdir -p build/hex
cp -- "$hex_source" build/hex/IDU-pre.hex
echo "HEX exported: $workspace_dir/build/hex/IDU-pre.hex"
