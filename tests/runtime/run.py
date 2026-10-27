"""在独立 native 进程中执行真实应用故障场景。"""
import os
import subprocess
import sys

executable, policy, *cases = sys.argv[1:]
if not cases:
    cases = ["publish", "sample", "led", "led-init", "input-init", "maintenance"]
    cases.extend(["task-timeout", "hold"])
    if policy == "debug":
        cases.append("force-reset")
for case in cases:
    reset = policy in ("reset", "debug") and case not in ("led-init", "input-init", "protocol-init", "hold")
    reset = reset or case == "force-reset"
    marker = "RUNTIME_RESET_PASS" if reset else "RUNTIME_POLICY_PASS"
    result = subprocess.run([executable, "-flash_in_ram"],
                            env={**os.environ, "IDU_RUNTIME_CASE": case},
                            capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode or marker not in result.stdout:
        raise SystemExit(f"Runtime {policy}/{case} failed: {result.returncode}")
    if case in ("led-init", "input-init", "protocol-init", "hold"):
        if result.stdout.count("Terminal fault:") < 2:
            raise SystemExit(f"Runtime {policy}/{case} did not repeat terminal diagnostics")
print(f"Runtime {policy}: {len(cases)} fresh-process scenarios passed")
