"""Execute real application fault scenarios in fresh native processes."""
import os
import subprocess
import sys

executable, policy, *cases = sys.argv[1:]
if not cases:
    cases = ["publish", "sample", "led", "input-init", "maintenance"]
    if policy != "debug":
        cases.append("task-timeout")
marker = "RUNTIME_RESET_PASS" if policy == "reset" else "RUNTIME_POLICY_PASS"
for case in cases:
    result = subprocess.run([executable, "-flash_in_ram"],
                            env={**os.environ, "IDU_RUNTIME_CASE": case},
                            capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode or marker not in result.stdout:
        raise SystemExit(f"Runtime {policy}/{case} failed: {result.returncode}")
print(f"Runtime {policy}: {len(cases)} fresh-process scenarios passed")
