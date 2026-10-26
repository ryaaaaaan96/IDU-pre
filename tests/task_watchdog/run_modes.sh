#!/usr/bin/env bash
set -euo pipefail
workspace_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../../../.." && pwd)"
cd -- "$workspace_dir"
for mode in both software external neither; do
    case "$mode" in
      both) sw=y; hw=y;;
      software) sw=y; hw=n;;
      external) sw=n; hw=y;;
      neither) sw=n; hw=n;;
    esac
    dir="build/IDU-pre/tests/watchdog-$mode"
    env ZEPHYR_TOOLCHAIN_VARIANT=host CCACHE_DIR=/tmp/idu-ccache XDG_CACHE_HOME=/tmp/idu-cache .venv/bin/west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/task_watchdog -d "$dir" -- -DCONFIG_WATCHDOG_SOFTWARE="$sw" -DCONFIG_WATCHDOG_EXTERNAL="$hw" > "build/watchdog-$mode.log" 2>&1
    if [ "$sw" = y ]; then
      .venv/bin/python applications/IDU-pre/tests/task_watchdog/run.py "$dir/zephyr/zephyr.exe" > "build/watchdog-$mode-tests.log" 2>&1
    else
      "$dir/zephyr/zephyr.exe" -test=task_watchdog::test_configuration_switches > "build/watchdog-$mode-tests.log" 2>&1
    fi
done

# 显式初始化失败也必须保留错误并由 status 留驻，不启动喂狗。
dir="build/IDU-pre/tests/watchdog-init-failure"
env ZEPHYR_TOOLCHAIN_VARIANT=host CCACHE_DIR=/tmp/idu-ccache XDG_CACHE_HOME=/tmp/idu-cache .venv/bin/west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/task_watchdog -d "$dir" -- -DCONFIG_TEST_WATCHDOG_INIT_FAILURE=y > build/watchdog-init-failure.log 2>&1
"$dir/zephyr/zephyr.exe" -test=task_watchdog::test_explicit_initialization_failure > build/watchdog-init-failure-tests.log 2>&1
