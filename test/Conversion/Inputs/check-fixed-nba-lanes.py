"""Check the fixed-lane proof's alias and reflection boundaries in MLIR."""

import pathlib
import subprocess
import sys

source = pathlib.Path(sys.argv[1]).read_text()
opt = sys.argv[2]
pipeline = (
    "builtin.module(obelisk_sim.design("
    "obelisk_sim.func(obelisk-sim-thread-process-cfg),"
    "obelisk-sim-build-compute-graph{vpi=MODE},"
    "obelisk-sim-verify-compute-graph,"
    "obelisk-sim-materialize-graph-regions,"
    "obelisk-sim-materialize-compute-fusion,"
    "obelisk-sim-specialize-static-state-nba,"
    "obelisk-sim-plan-static-superstep),"
    "encode-obelisk-sim-to-bytecode{vpi=MODE},"
    "convert-obelisk-sim-processes-to-llvm-coroutines)"
)
target = "%b = obelisk_sim.ref.subelement %data[[1]] : !dataref -> !obelisk_sim.ref<!word>"
assert source.count(target) == 1
cases = {
    "disjoint": source,
    "exact-alias": source.replace(target, target.replace("[[1]]", "[[0]]")),
    "partial-alias": source.replace(
        target,
        "%b = obelisk_sim.ref.extract %data from 16 : !dataref -> !obelisk_sim.ref<!word>",
    ),
}
for mode in ("off", "read", "full"):
    for name, text in cases.items():
        result = subprocess.run(
            [opt, "--pass-pipeline=" + pipeline.replace("MODE", mode)],
            input=text, text=True, capture_output=True, timeout=60,
        )
        assert result.returncode == 0, (mode, name, result.stderr)
        slots = result.stdout.count("llvm.mlir.global internal @__obelisk_eval_nba_valid_")
        if name == "disjoint":
            assert slots == 3, (mode, name, slots)
            assert "llvm.func @__obelisk_eval_dispatch_v1(" in result.stdout
        else:
            assert slots == 0, (mode, name, slots)
            assert "llvm.func @__obelisk_eval_dispatch_v1(" not in result.stdout
