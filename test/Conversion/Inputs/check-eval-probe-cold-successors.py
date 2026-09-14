"""A checkpoint successor can still be reachable through another hot edge."""

from pathlib import Path
import subprocess
import sys

source, prefix, obelisk_opt = sys.argv[1:]
fixture = Path(source).read_text().replace(
    "cf.cond_br %bad, ^cold, ^wait", "cf.cond_br %bad, ^cold, ^after_cold")
path = prefix + ".hot.mlir"
Path(path).write_text(fixture)
pipeline = (
    "builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),"
    "obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,"
    "obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,"
    "obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),"
    "convert-obelisk-sim-processes-to-llvm-coroutines)"
)
result = subprocess.run([obelisk_opt, path, "--pass-pipeline=" + pipeline],
                        capture_output=True, text=True, timeout=30)
assert result.returncode != 0, result.stdout
assert "unguarded runtime leaf" in result.stderr, result.stderr
