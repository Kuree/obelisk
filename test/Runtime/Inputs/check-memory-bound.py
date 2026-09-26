"""Run a small MLIR-generated executable and bound its peak resident memory."""
import resource
import subprocess
import sys

subprocess.run(sys.argv[1:], check=True)
peak = resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss
if sys.platform == "darwin":
    peak //= 1024
# Plenty of headroom above startup (~4 MiB), but below the old 260 MiB
# retained by ten million obsolete native delay entries. No timing assertion.
assert peak < 64 * 1024, f"peak RSS {peak} KiB exceeds 64 MiB"
