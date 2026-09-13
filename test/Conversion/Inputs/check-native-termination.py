"""Compare explicit termination across generic, AOT, and generated eval."""

import os
from pathlib import Path
import re
import subprocess
import sys

source, prefix, obelisk_opt, llvm_bin, support = sys.argv[1:]
template = Path(source).read_text()
pipeline = (
    "builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),"
    "obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,"
    "obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,"
    "obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),"
    "convert-obelisk-sim-processes-to-llvm-coroutines)"
)


def run(args, **kwargs):
    return subprocess.run(args, check=True, timeout=60, **kwargs)


for kind in ["finish", "fatal", "stop"]:
    for mode in [1, 0, 2, 3]:
        base = f"{prefix}.{kind}.{mode}"
        fixture = template.replace("obelisk.native_scheduler = 0 : i32",
                                   f"obelisk.native_scheduler = {mode} : i32")
        fixture = fixture.replace("obelisk_sim.fatal %ctx", f"obelisk_sim.{kind} %ctx")
        Path(base + ".mlir").write_text(fixture)
        run([obelisk_opt, base + ".mlir", "--pass-pipeline=" + pipeline,
             "-o", base + ".llvm.mlir"])
        generated = Path(base + ".llvm.mlir").read_text()
        if mode != 1:
            assert "__obelisk_aot_schedule_plan_v1" in generated, (kind, mode)
        if mode == 3:
            assert "obelisk_rt_v1_scheduler_prepare_periodic_aot" in generated
        run([llvm_bin + "/mlir-translate", "--mlir-to-llvmir", base + ".llvm.mlir",
             "-o", base + ".ll"])
        run([llvm_bin + "/opt", "-passes=coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>",
             base + ".ll", "-o", base + ".bc"])
        run([llvm_bin + "/llc", "-filetype=obj", "-relocation-model=pic",
             base + ".bc", "-o", base + ".o"])
        run([llvm_bin + "/clang++", base + ".o", *[
            support + "/" + lib for lib in
            ["libobelisk_rt.a", "libc++.a", "libc++abi.a", "libunwind.a"]],
            "-nostdlib++", "-lpthread", "-ldl", "-o", base + ".exe"])
        result = subprocess.run([base + ".exe"], capture_output=True, text=True,
                                timeout=10,
                                env=dict(os.environ, OBELISK_RT_SIGNAL_DIAGNOSTICS="1"))
        assert result.stdout.strip() == "final=500", (kind, mode, result)
        assert (result.returncode != 0) == (kind == "fatal"), (kind, mode, result)
        if mode == 3:
            # Untaken leaves must not turn all 500 edges into runtime work.
            iterations = int(re.search(r"scheduler_iterations=(\d+)", result.stderr)[1])
            assert iterations < 50, (kind, mode, result.stderr)
        print(f"{kind} scheduler={mode}: PASS", flush=True)
