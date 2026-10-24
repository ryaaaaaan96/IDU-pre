"""Run acquisition lifecycle cases with one product startup per native process."""
import re
import subprocess
import sys

executable = sys.argv[1]
listing = subprocess.run([executable, "-list"], capture_output=True, text=True, timeout=30)
# Ztest may report an unrun suite on -list; validate the discovered cases instead.
cases = re.findall(r"^acquisition::test_\w+$", listing.stdout, re.M)
if not cases:
    raise SystemExit("No acquisition cases found: " + listing.stdout + listing.stderr)
for case in cases:
    result = subprocess.run([executable, "-test=" + case], capture_output=True, text=True, timeout=30)
    if result.returncode or "PROJECT EXECUTION SUCCESSFUL" not in result.stdout:
        print(result.stdout, end="")
        print(result.stderr, end="", file=sys.stderr)
        raise SystemExit("Acquisition case failed: " + case)
    print("PASS: " + case)
print(f"Acquisition acceptance: {len(cases)} fresh-process cases passed")
