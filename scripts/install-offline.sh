#!/usr/bin/env bash
# 将此脚本与离线包放在同一目录后执行；不修改系统 Python 环境。
set -euo pipefail

script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
archive="$script_dir/IDU-pre-offline-linux-x86_64.tar.gz"
destination="$script_dir/IDU-pre-offline"
install_system=true
build=false
temporary_dir=""

usage() {
    cat <<'EOF'
Usage: ./install-offline.sh [options]
  --archive FILE       Offline archive (default: next to this script)
  --dest DIRECTORY     Extracted workspace (default: ./IDU-pre-offline next to script)
  --skip-system-deps   Skip system installation; check existing dependencies instead
  --build              Build firmware after preparing the environment
  -h, --help           Show this help

Requires Linux x86_64 with Python >= 3.12 (required by Zephyr).
Ubuntu/Debian use apt-get; Fedora uses dnf.
IDU_PYTHON can select a specific Python executable.
System package installation requires network access and sudo.
Python dependencies and firmware builds use only bundled files.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --archive|--dest)
            if [[ $# -lt 2 || -z "$2" ]]; then
                echo "Missing value for $1" >&2
                exit 2
            fi
            if [[ "$1" == --archive ]]; then
                archive="$2"
            else
                destination="$2"
            fi
            shift 2
            ;;
        --skip-system-deps) install_system=false; shift ;;
        --build) build=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) echo "Unknown option: $1" >&2; usage >&2; exit 2 ;;
    esac
done

if [[ "$(uname -sm)" != 'Linux x86_64' ]]; then
    echo "Use Linux x86_64 or WSL2 Ubuntu." >&2
    exit 2
fi
if [[ ! -f "$archive" ]]; then
    echo "Archive not found: $archive" >&2
    exit 2
fi
archive="$(realpath -- "$archive")"
destination="$(realpath -m -- "$destination")"
if [[ -e "$destination" ]]; then
    echo "Destination already exists: $destination. Choose another --dest." >&2
    exit 2
fi
if [[ -f "$archive.sha256" ]]; then
    (
        cd -- "$(dirname -- "$archive")"
        sha256sum -c "$(basename -- "$archive").sha256"
    )
fi

# 优先复用系统解释器；显式指定时不静默切换到其他版本。
select_python() {
    local candidate
    local candidates=(python3 python3.14 python3.13 python3.12)
    if [[ -n "${IDU_PYTHON:-}" ]]; then
        candidates=("$IDU_PYTHON")
    fi
    for candidate in "${candidates[@]}"; do
        if command -v "$candidate" >/dev/null && "$candidate" -c '
import sys
sys.exit(0 if sys.version_info >= (3, 12) else 1)
' 2>/dev/null; then
            python_bin="$candidate"
            return 0
        fi
    done
    return 1
}

dependencies_ready() {
    select_python && command -v git >/dev/null && command -v dtc >/dev/null &&
        "$python_bin" -c 'import venv, ensurepip' 2>/dev/null
}

if $install_system && ! dependencies_ready; then
    distro_id=""
    distro_like=""
    if [[ -r /etc/os-release ]]; then
        distro_id="$(. /etc/os-release; printf '%s' "${ID:-}")"
        distro_like="$(. /etc/os-release; printf '%s' "${ID_LIKE:-}")"
    fi
    elevate=()
    if [[ "$EUID" -ne 0 ]]; then
        elevate=(sudo)
    fi
    case " $distro_id $distro_like " in
        *" ubuntu "*|*" debian "*)
            echo "Installing system dependencies with apt-get..."
            "${elevate[@]}" apt-get update
            "${elevate[@]}" apt-get install -y \
                python3 python3-venv git device-tree-compiler libstdc++6
            ;;
        *" fedora "*|*" rhel "*)
            echo "Installing system dependencies with dnf..."
            "${elevate[@]}" dnf install -y python3 python3-pip git dtc libstdc++
            ;;
        *)
            echo "Unsupported automatic system setup: ${distro_id:-unknown distribution}." >&2
            echo "Install Python >= 3.12 with venv/ensurepip, git and dtc," >&2
            echo "then use --skip-system-deps." >&2
            exit 2
            ;;
    esac
fi

if ! select_python; then
    echo "Zephyr requires Python >= 3.12; no suitable interpreter was found." >&2
    echo "Install a newer interpreter or set IDU_PYTHON=/path/to/python3." >&2
    exit 2
fi
for command in "$python_bin" git dtc tar sha256sum; do
    if ! command -v "$command" >/dev/null; then
        echo "Required command not found: $command" >&2
        exit 2
    fi
done
"$python_bin" -c '
import sys
if sys.version_info < (3, 12):
    sys.exit("Zephyr requires Python >= 3.12.")
import venv, ensurepip
print("Selected Python:", sys.executable, sys.version.split()[0])
'

# 先完整解压、校验再发布目标目录；虚拟环境必须在最终路径创建。
mkdir -p -- "$(dirname -- "$destination")"
temporary_dir="$(mktemp -d "$(dirname -- "$destination")/.idu-extract.XXXXXX")"
trap 'if [[ -n "$temporary_dir" ]]; then rm -rf -- "$temporary_dir"; fi' EXIT
tar -xzf "$archive" -C "$temporary_dir"
extracted="$temporary_dir/IDU-pre-offline"
if [[ ! -f "$extracted/build.sh" || ! -f "$extracted/requirements.txt" ||
      ! -d "$extracted/wheels" || ! -f "$extracted/SHA256SUMS" ]]; then
    echo "Archive is not an IDU-pre offline bundle." >&2
    exit 2
fi
echo "Verifying extracted files..."
if ! (cd -- "$extracted" && sha256sum --quiet -c SHA256SUMS); then
    echo "Extracted file verification failed." >&2
    exit 1
fi
mv -- "$extracted" "$destination"
rmdir -- "$temporary_dir"
temporary_dir=""
"$python_bin" -m venv "$destination/.venv"
if ! "$destination/.venv/bin/python" -m pip --disable-pip-version-check install \
    --no-index --find-links "$destination/wheels" -r "$destination/requirements.txt"; then
    echo "Offline dependency installation failed. Check the pip error above." >&2
    echo "This bundle includes native wheels for Python 3.12, 3.13 and 3.14." >&2
    echo "Other interpreters may require additional compatible wheels." >&2
    exit 1
fi
"$destination/toolchain/gnu/arm-zephyr-eabi/bin/arm-zephyr-eabi-gcc" --version

echo "Environment ready: $destination"
printf 'Build firmware: cd %q && ./build.sh --pristine always\n' "$destination"
if $build; then
    cd -- "$destination"
    exec ./build.sh --pristine always
fi
