#!/usr/bin/env bash
set -euo pipefail

app_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
workspace_dir="$(cd -- "$app_dir/../.." && pwd)"

if [[ "${1:-}" == --help || "${1:-}" == -h ]]; then
    echo "Usage: $0 [idu_pre] [--mcuboot] [--iapv3 | --smp-uart | --no-upload] [options...]"
    echo "All builds use MCUboot; IAPv3 is the default upload protocol."
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

# --mcuboot remains an optional selector for existing command lines.
if [[ "${1:-}" == --mcuboot ]]; then
    shift
fi
exec python3 "$app_dir/tools/build_mcuboot.py" "$@"
