#!/usr/bin/env bash
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
app_dir="$(cd -- "$script_dir/.." && pwd)"
workspace_dir="$(cd -- "$app_dir/../.." && pwd)"
build_dir="$workspace_dir/build/IDU-pre/mcuboot-iapv3/IDU-pre"
remote_host=127.0.0.1
remote_port=2331
elf_file=
gdb_program=
dry_run=false
load_elf=true

usage() {
    cat <<'EOF'
Usage: ./scripts/debug.sh [options] [-- extra GDB arguments]
  --host HOST       Windows GDB Server IPv4/IPv6 or hostname (default: 127.0.0.1)
  --port PORT       GDB TCP port (default: 2331)
  --build-dir DIR   Application domain build directory (relative to workspace)
  --elf FILE        ELF with symbols (relative to workspace)
  --gdb PROGRAM     Arm GDB executable; otherwise use CMakeCache.txt or PATH
  --attach          Only connect and halt; skip ELF download
  --dry-run         Print the command without connecting
  -h, --help        Show help

Default build: build/IDU-pre/mcuboot-iapv3/IDU-pre
NAT WSL: pass --host with the Windows address from `ip route show default`.
Connects, halts and downloads the ELF, then stays in GDB. No build/reset/run.
Use --attach for an existing signed MCUboot image. Raw ELF downloads may
invalidate MCUboot verification; reinstall signed firmware with flash.sh.
See docs/wsl-jlink.md for Zephyr thread support and external watchdog handling.
EOF
}

fail() { printf 'debug.sh: %s\n' "$*" >&2; exit 2; }
need_value() { [[ $# -ge 2 && -n "$2" && "$2" != --* ]] || fail "Missing value for $1"; }

while [[ $# -gt 0 ]]; do
    case "$1" in
        --host) need_value "$@"; remote_host=$2; shift 2 ;;
        --port) need_value "$@"; remote_port=$2; shift 2 ;;
        --build-dir) need_value "$@"; build_dir=$2; shift 2 ;;
        --elf) need_value "$@"; elf_file=$2; shift 2 ;;
        --gdb) need_value "$@"; gdb_program=$2; shift 2 ;;
        --dry-run) dry_run=true; shift ;;
        --attach) load_elf=false; shift ;;
        -h|--help) usage; exit 0 ;;
        --) shift; break ;;
        *) fail "Unknown option: $1 (use --help)" ;;
    esac
done

[[ "$remote_host" =~ ^[a-zA-Z0-9_:][a-zA-Z0-9_.:-]*$ ]] || fail 'Invalid host'
[[ "$remote_port" =~ ^[0-9]{1,5}$ ]] || fail 'Port must be between 1 and 65535'
remote_port=$((10#$remote_port))
(( remote_port >= 1 && remote_port <= 65535 )) || fail 'Port must be between 1 and 65535'

cd -- "$workspace_dir"
[[ "$build_dir" == /* ]] || build_dir="$workspace_dir/$build_dir"
if [[ -z "$elf_file" ]]; then
    kernel_name=zephyr
    if [[ -f "$build_dir/zephyr/.config" ]]; then
        configured_name=$(sed -n 's/^CONFIG_KERNEL_BIN_NAME="\([^"]*\)"$/\1/p' "$build_dir/zephyr/.config")
        [[ -z "$configured_name" ]] || kernel_name=$configured_name
    fi
    elf_file="$build_dir/zephyr/$kernel_name.elf"
fi
[[ "$elf_file" == /* ]] || elf_file="$workspace_dir/$elf_file"
[[ -f "$elf_file" ]] || fail "ELF not found: $elf_file; build first or use --elf"
[[ "$elf_file" != *$'\n'* && "$elf_file" != *$'\r'* ]] || fail 'ELF path contains a newline'

if [[ -z "$gdb_program" && -f "$build_dir/CMakeCache.txt" ]]; then
    cached_gdb=$(sed -n 's/^CMAKE_GDB:[^=]*=//p' "$build_dir/CMakeCache.txt")
    [[ ! -x "$cached_gdb" ]] || gdb_program=$cached_gdb
fi
if [[ -z "$gdb_program" ]]; then
    for candidate in arm-zephyr-eabi-gdb arm-none-eabi-gdb gdb-multiarch; do
        if command -v "$candidate" >/dev/null 2>&1; then gdb_program=$candidate; break; fi
    done
fi
[[ -n "$gdb_program" ]] || fail 'Arm GDB not found; supply --gdb or build with the Zephyr SDK first'
resolved_gdb=$(command -v -- "$gdb_program") || fail "GDB executable not found: $gdb_program"
gdb_program=$resolved_gdb
[[ -x "$gdb_program" ]] || fail "GDB is not executable: $gdb_program"

target="$remote_host:$remote_port"
[[ "$remote_host" != *:* ]] || target="[$remote_host]:$remote_port"
command_line=("$gdb_program" -q -nx "$elf_file" -ex "target remote $target" -ex 'monitor halt')
if "$load_elf"; then
    quoted_elf=${elf_file//\\/\\\\}
    quoted_elf=${quoted_elf//\"/\\\"}
    command_line+=(-ex "load \"$quoted_elf\"")
fi
command_line+=("$@")
printf 'GDB target: %s\nELF: %s\n' "$target" "$elf_file" >&2
if "$load_elf"; then printf 'ELF download enabled (raw development image).\n' >&2; fi
if "$dry_run"; then
    printf '%q ' "${command_line[@]}"
    printf '\n'
    exit 0
fi
exec "${command_line[@]}"
