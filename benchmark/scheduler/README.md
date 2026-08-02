# Eight-lane NBA scheduler microbenchmark

`nba8.sv` keeps eight edge waiters active and publishes five nonblocking
updates per lane and cycle, including a conditional same-destination
overwrite. The runner builds optimized native and compact-bytecode executables,
performs one warm-up and five measured runs at 100,000, 200,000, 400,000, and
1,000,000 cycles with 0, 1024, and 3072 dormant waiters. The runner
checks every lane against an independent expected-value model, and emits JSON
containing median wall time, RSS, throughput, doubling ratios, and runtime
subscription/AOT counters. AOT samples fail if they perform generic candidate
scans, readiness calls, or scheduler fallback.

Run it from the repository root:

```sh
python3 benchmark/scheduler/run_nba8.py \
  --output tmp/nba8-results.json
```

Pass `--verilator /path/to/verilator` to compile the same source with Verilator
and require identical lane output. Override `--waiters` to measure other
dormant-fanout populations. Use the generated executables with
Callgrind when instruction-level profiles are needed; `--tier native` and
`--tier bytecode` isolate the two Obelisk configurations, while
`--native-scheduler=generic` selects the semantic oracle. Design-wide
bytecode operations are included wherever the workload lowers to them; the
driver does not expose a third, independent design-task-only tier switch.

## Gated convergence SCC

`gated_scc.sv` is an FPGA/CGRA-shaped mixed-tier microbenchmark. A clocked
controller and accumulator surround two gated monotone combinational equations
that form one convergence SCC. The generated schedule keeps the controller in
Tier 1 and emits a direct Tier-2 dirty-mask subkernel whose inactive ingress is
one branch; disabling the gate settles the SCC to zero. Build both
`--native-scheduler=auto` and `--native-scheduler=generic` as the correctness
oracle, then compare the single `GATED_SCC` result line. `-emit-llvm` exposes
the planned `__obelisk_tier1_eval_step_v1` to
`__obelisk_tier2_converge_v1_*` helper edge. The installed schedule does not
yet call that helper; this benchmark therefore remains a semantic and codegen
fixture, not evidence of hot-path Tier-2 reachability.
This benchmark intentionally has no Verilator step.

For a quick local comparison:

```sh
build/tools/driver/obelisk -O3 --native-scheduler=generic \
  benchmark/scheduler/gated_scc.sv -o tmp/gated-scc-generic
build/tools/driver/obelisk -O3 --native-scheduler=auto \
  benchmark/scheduler/gated_scc.sv -o tmp/gated-scc-auto
diff <(tmp/gated-scc-generic) <(tmp/gated-scc-auto)
```
