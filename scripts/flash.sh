#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$(cd -- "$script_dir/.." && pwd)"
workspace_dir="$(cd -- "$app_dir/../.." && pwd)"
build_dir="$workspace_dir/build/IDU-pre/mcuboot-iapv3/IDU-pre"
hex_file="$workspace_dir/build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-factory.hex"
remote_host=127.0.0.1
remote_port=2331
gdb_program=
dry_run=false

usage() {
    cat <<'EOF'
Usage: ./scripts/flash.sh [options]
  --host HOST       Existing J-Link GDB Server (default: 127.0.0.1)
  --port PORT       GDB TCP port (default: 2331)
  --hex FILE        Combined MCUboot + signed application factory HEX
  --build-dir DIR   Application domain used to locate GDB in CMakeCache.txt
  --gdb PROGRAM     Arm GDB executable; otherwise use the build cache or PATH
  --dry-run         Validate the HEX and print commands without connecting
  -h, --help        Show help

Default HEX: build/hex/IDU-pre-mcuboot-iapv3/IDU-pre-factory.hex
Relative paths are resolved from the workspace root.
Full installation: erase MCU internal flash, program the factory HEX, reset
and run, then exit. External SPI NOR is not erased. No build or debug session.
Start Windows J-Link GDB Server first; NAT WSL needs --host WINDOWS_IP.
EOF
}
fail() { printf 'flash.sh: %s\n' "$*" >&2; exit 2; }
need_value() { [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || fail "Missing value for $1"; }
while [[ $# -gt 0 ]]; do
    case "$1" in
        --host) need_value "$@"; remote_host=$2; shift 2 ;;
        --port) need_value "$@"; remote_port=$2; shift 2 ;;
        --hex) need_value "$@"; hex_file=$2; shift 2 ;;
        --build-dir) need_value "$@"; build_dir=$2; shift 2 ;;
        --gdb) need_value "$@"; gdb_program=$2; shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) fail "Unknown option: $1 (use --help)" ;;
    esac
done

[[ "$remote_host" =~ ^[a-zA-Z0-9_:][a-zA-Z0-9_.:-]*$ ]] || fail 'Invalid host'
[[ "$remote_port" =~ ^[0-9]{1,5}$ ]] || fail 'Port must be between 1 and 65535'
remote_port=$((10#$remote_port))
(( remote_port >= 1 && remote_port <= 65535 )) || fail 'Port must be between 1 and 65535'
cd -- "$workspace_dir"
[[ "$hex_file" == /* ]] || hex_file="$workspace_dir/$hex_file"
[[ "$build_dir" == /* ]] || build_dir="$workspace_dir/$build_dir"
[[ -f "$hex_file" ]] || fail "Factory HEX not found: $hex_file; run scripts/build.sh first"
[[ "$hex_file" != *$'\n'* && "$hex_file" != *$'\r'* ]] || fail 'HEX path contains a newline'

# 连接或擦除前，拒绝仅含应用或无效的镜像。
python3 "$app_dir/tools/check_factory_hex.py" "$hex_file"

if [[ -z "$gdb_program" && -f "$build_dir/CMakeCache.txt" ]]; then
    cached_gdb=$(sed -n 's/^CMAKE_GDB:[^=]*=//p' "$build_dir/CMakeCache.txt")
    [[ ! -x "$cached_gdb" ]] || gdb_program=$cached_gdb
fi
if [[ -z "$gdb_program" ]]; then
    for candidate in arm-zephyr-eabi-gdb arm-none-eabi-gdb gdb-multiarch; do
        if command -v "$candidate" >/dev/null 2>&1; then gdb_program=$candidate; break; fi
    done
fi
[[ -n "$gdb_program" ]] || fail 'Arm GDB not found; supply --gdb'
resolved_gdb=$(command -v -- "$gdb_program") || fail "GDB executable not found: $gdb_program"
[[ -x "$resolved_gdb" ]] || fail "GDB is not executable: $resolved_gdb"
target="$remote_host:$remote_port"
[[ "$remote_host" != *:* ]] || target="[$remote_host]:$remote_port"
quoted_hex=${hex_file//\\/\\\\}
quoted_hex=${quoted_hex//\"/\\\"}

commands=$(cat <<EOF
set pagination off
set confirm off
set gnutarget ihex
file "$quoted_hex"
target remote $target
monitor halt
monitor reset
monitor exec SetVerifyDownload = 1
monitor flash erase
load "$quoted_hex"
monitor reset
monitor go
disconnect
quit
EOF
)
printf 'Factory installation: erase MCU internal flash and program %s\nGDB target: %s\n' "$hex_file" "$target" >&2
if "$dry_run"; then
    printf 'GDB: %s\n%s\n' "$resolved_gdb" "$commands"
    exit 0
fi

command_file=$(mktemp "${TMPDIR:-/tmp}/idu-flash.XXXXXX")
trap 'rm -f -- "$command_file"' EXIT
printf '%s\n' "$commands" > "$command_file"
# 命令文件在首个 GDB 错误处停止；下载失败时不再复位或运行。
"$resolved_gdb" -q -nx -batch -x "$command_file"
printf 'Firmware programmed; reset and run commands completed.\n'
