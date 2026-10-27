#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$(cd -- "$script_dir/.." && pwd)"
workspace_dir="$(cd -- "$app_dir/../.." && pwd)"

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    echo "Usage: $0 [idu_pre] [--mcuboot] [--iapv3 | --no-upload] [options...]"
    echo "All builds use MCUboot; IAPv3 is the only upload protocol."
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

# 保留 --mcuboot，兼容已有命令行中的可选选择参数。
if [[ "${1:-}" == --mcuboot ]]; then
    shift
fi
exec python3 "$app_dir/tools/build_mcuboot.py" "$@"
