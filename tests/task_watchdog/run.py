"""每个状态场景使用独立 native 进程；故障为终止状态。"""
import re
import subprocess
import sys

executable = sys.argv[1]
listing = subprocess.run([executable, "-list"], capture_output=True, text=True, timeout=30)
cases = re.findall(r"^task_watchdog::test_\w+$", listing.stdout, re.M)
if not cases:
    raise SystemExit("No task watchdog scenarios found: " + listing.stdout + listing.stderr)
for case in cases:
    result = subprocess.run([executable, "-test=" + case], capture_output=True, text=True, timeout=30)
    print(result.stdout, end="")
    print(result.stderr, end="", file=sys.stderr)
    if result.returncode:
        raise SystemExit(result.returncode)
    marker = "WATCHDOG_RESET_PASS" if "stops_and_reboots" in case else "PROJECT EXECUTION SUCCESSFUL"
    if marker not in result.stdout:
        raise SystemExit("Scenario did not reach its acceptance boundary: " + case)
print(f"Task watchdog acceptance: {len(cases)} fresh-process scenarios passed")
