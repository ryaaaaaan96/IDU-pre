#!/usr/bin/env bash
# 解压包内入口：只使用随包依赖，在新虚拟环境中构建。
set -euo pipefail
bundle_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
if [[ ! -d "$bundle_dir/wheels" ]]; then
    echo "Run the build.sh at the root of the extracted offline bundle." >&2
    exit 2
fi
if [[ "$(uname -sm)" != 'Linux x86_64' ]]; then
    echo "This bundle requires Linux x86_64 (Windows: use WSL2 Ubuntu)." >&2
    exit 2
fi
python_bin="${IDU_PYTHON:-python3}"
if [[ -x "$bundle_dir/.venv/bin/python" ]]; then
    python_bin="$bundle_dir/.venv/bin/python"
fi
"$python_bin" -c 'import sys; assert sys.version_info >= (3, 12), "Zephyr requires Python >= 3.12"'
for command in git dtc; do
    if ! command -v "$command" >/dev/null; then
        echo "Missing $command. Install: sudo apt install python3-venv git device-tree-compiler" >&2
        exit 2
    fi
done
cd -- "$bundle_dir"
if [[ ! -x .venv/bin/python ]]; then
    "$python_bin" -m venv .venv
fi
source .venv/bin/activate
python -m pip install --no-index --find-links "$bundle_dir/wheels" \
    -r "$bundle_dir/requirements.txt"
# 覆盖旧电脑或调用者的工具链配置，不依赖全局 CMake 注册。
export ZEPHYR_BASE="$bundle_dir/zephyr"
export ZEPHYR_TOOLCHAIN_VARIANT=zephyr
export ZEPHYR_SDK_INSTALL_DIR="$bundle_dir/toolchain"
export CCACHE_DISABLE=1
unset CROSS_COMPILE GNUARMEMB_TOOLCHAIN_PATH ARMCLANG_TOOLCHAIN_PATH
exec ./applications/IDU-pre/scripts/build.sh "$@"
