"""Check disjoint and aliasing slices at the native checkpoint boundary."""

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
for offset in [8, 4, 0]:
    fixture = template.replace("%src = obelisk_sim.ref.extract %data from 8",
                               f"%src = obelisk_sim.ref.extract %data from {offset}")
    path = f"{prefix}.{offset}.mlir"
    Path(path).write_text(fixture)
    result = subprocess.run([obelisk_opt, path, "--pass-pipeline=" + pipeline],
                            capture_output=True, text=True, timeout=30)
    if offset == 8:
        assert result.returncode == 0, result.stderr
        assert "work.__obelisk_eval_body_0.__obelisk_path_known" in result.stdout
        assert "work.__obelisk_eval_body_0.__obelisk_checkpoint_path" in result.stdout
        assert "obelisk_rt_v1_scheduler_prepare_periodic_aot" in result.stdout
    else:
        assert result.returncode != 0, (offset, result.stdout)
        assert "unguarded runtime leaf" in result.stderr, result.stderr
    print(f"read offset={offset}: PASS", flush=True)
