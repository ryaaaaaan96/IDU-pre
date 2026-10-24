"""在独立 native 进程中运行采样生命周期场景，每个进程仅启动产品一次。"""
import re
import os
import subprocess
import sys

executable = sys.argv[1]
listing = subprocess.run([executable, "-list"], capture_output=True, text=True, timeout=30)
# Ztest 的 -list 可能报告套件未运行，应校验实际发现的用例列表。
cases = re.findall(r"^acquisition::test_\w+$", listing.stdout, re.M)
if not cases:
    raise SystemExit("No acquisition cases found: " + listing.stdout + listing.stderr)
for case in cases:
    result = subprocess.run([executable, "-test=" + case], env={**os.environ, "IDU_ACQUISITION_CASE": case},
                            capture_output=True, text=True, timeout=30)
    if result.returncode or "PROJECT EXECUTION SUCCESSFUL" not in result.stdout:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise SystemExit("Acquisition case failed: " + case)
    print("PASS: " + case)
print(f"Acquisition acceptance: {len(cases)} fresh-process cases passed")
