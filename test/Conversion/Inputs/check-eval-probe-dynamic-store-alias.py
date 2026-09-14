"""Distinguish bounded dynamic selects from unbounded memory aliases."""

from pathlib import Path
import subprocess
import sys

source, prefix, obelisk_opt = sys.argv[1:]
template = Path(source).read_text()
pipeline = (
    "builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),"
    "obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,"
    "obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,"
    "obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),"
    "convert-obelisk-sim-processes-to-llvm-coroutines)"
)
for root in [3, 1]:
    fixture = template.replace("%other = obelisk_sim.context.storage %ctx[3]",
                               f"%other = obelisk_sim.context.storage %ctx[{root}]")
    path = f"{prefix}.{root}.mlir"
    Path(path).write_text(fixture)
    result = subprocess.run([obelisk_opt, path, "--pass-pipeline=" + pipeline],
                            capture_output=True, text=True, timeout=30)
    if root == 3:
        assert result.returncode == 0, result.stderr
        assert "obelisk.eval.generated" in result.stdout
        assert "work.__obelisk_eval_body_0.__obelisk_checkpoint_path" in result.stdout
    else:
        assert result.returncode != 0, result.stdout
        assert "unguarded runtime leaf" in result.stderr, result.stderr
    print(f"dynamic read root={root}: PASS", flush=True)
