"""在独立 native 进程中执行真实应用故障场景。"""
import os
import subprocess
import sys

executable, policy, *cases = sys.argv[1:]
if not cases:
    cases = ["publish", "sample", "led", "led-init", "input-init", "maintenance"]
    if policy != "debug":
        cases.append("task-timeout")
    else:
        cases.append("force-reset")
for case in cases:
    # 运行期 LED 允许继续的配置也不能放过启动期引脚初始化失败。
    reset = policy == "reset" or case == "force-reset" or (policy == "continue" and case == "led-init")
    marker = "RUNTIME_RESET_PASS" if reset else "RUNTIME_POLICY_PASS"
    result = subprocess.run([executable, "-flash_in_ram"],
                            env={**os.environ, "IDU_RUNTIME_CASE": case},
                            capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode or marker not in result.stdout:
        raise SystemExit(f"Runtime {policy}/{case} failed: {result.returncode}")
print(f"Runtime {policy}: {len(cases)} fresh-process scenarios passed")
