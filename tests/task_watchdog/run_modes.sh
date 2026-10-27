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
      "$dir/zephyr/zephyr.exe" -test=task_watchdog::test_direct_reset_stops_and_reboots > "build/watchdog-$mode-reset-tests.log" 2>&1
    fi
done

# 显式初始化失败也必须保留错误并由 status 留驻，不启动喂狗。
dir="build/IDU-pre/tests/watchdog-init-failure"
env ZEPHYR_TOOLCHAIN_VARIANT=host CCACHE_DIR=/tmp/idu-ccache XDG_CACHE_HOME=/tmp/idu-cache .venv/bin/west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/task_watchdog -d "$dir" -- -DCONFIG_TEST_WATCHDOG_INIT_FAILURE=y > build/watchdog-init-failure.log 2>&1
"$dir/zephyr/zephyr.exe" -test=task_watchdog::test_explicit_initialization_failure > build/watchdog-init-failure-tests.log 2>&1

# 首次检查未能运行时，启动宽限也必须到期。
dir="build/IDU-pre/tests/watchdog-startup-grace"
env ZEPHYR_TOOLCHAIN_VARIANT=host CCACHE_DIR=/tmp/idu-ccache XDG_CACHE_HOME=/tmp/idu-cache .venv/bin/west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/task_watchdog -d "$dir" -- -DCONFIG_TEST_WATCHDOG_BLOCK_FIRST_CHECK=y > build/watchdog-startup-grace.log 2>&1
"$dir/zephyr/zephyr.exe" -test=task_watchdog::test_startup_grace_expires_before_first_check > build/watchdog-startup-grace-tests.log 2>&1

# 软件周期改变后，有效期仍为三倍，不受固定硬件周期影响。
for period in 25 200; do
    dir="build/IDU-pre/tests/watchdog-period-$period"
    env ZEPHYR_TOOLCHAIN_VARIANT=host CCACHE_DIR=/tmp/idu-ccache XDG_CACHE_HOME=/tmp/idu-cache .venv/bin/west build -p auto -b native_sim/native/64 applications/IDU-pre/tests/task_watchdog -d "$dir" -- -DCONFIG_WATCHDOG_CHECK_INTERVAL_MS="$period" > "build/watchdog-period-$period.log" 2>&1
    for name in test_short_queue_delay_does_not_expire_health test_check_expiry_with_blocked_workqueue_stops_and_reboots test_heartbeats_do_not_renew_health_stops_and_reboots; do
        "$dir/zephyr/zephyr.exe" -test="task_watchdog::$name" > "build/watchdog-period-$period-$name.log" 2>&1
    done
done
