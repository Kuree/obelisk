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
that form one convergence SCC; disabling the gate settles the SCC to zero.
Build both
`--native-scheduler=auto` and `--native-scheduler=generic` as the correctness
oracle, then compare the single `GATED_SCC` result line. This fixture is not
currently eligible for the installed eval schedule: `auto` uses the generic
handoff and an explicit `eval` request reports the missing generated owner.
The disconnected Tier-2 materializer remains as a planning/codegen fixture,
and its standalone MLIR checks are explicitly labeled as materializer-only so
those helper symbols are not mistaken for evidence of installed reachability.
This benchmark intentionally has no Verilator step.

For a quick local comparison:

```sh
build/tools/driver/obelisk -O3 --native-scheduler=generic \
  benchmark/scheduler/gated_scc.sv -o tmp/gated-scc-generic
build/tools/driver/obelisk -O3 --native-scheduler=auto \
  benchmark/scheduler/gated_scc.sv -o tmp/gated-scc-auto
diff <(tmp/gated-scc-generic) <(tmp/gated-scc-auto)
```

## Performance coverage

CPU elapsed time is one acceptance signal, not a scheduling cost model. Use
larger designs together with controlled graph workloads to distinguish the
following costs:

| Workload | Vary | Measure |
| --- | --- | --- |
| Chains and reconvergent diamonds | Depth and helper-size splits | Instructions per activation, repeated evaluation, state loads/stores |
| Independent cones and sparse fanout | Total size at fixed active size | Empty ready scans and work charged to dormant logic |
| Configurable routing fabric | Potential SCC size and selected routes | Local convergence passes, affected tiers, scheduling-proof invalidation |
| Memory/interconnect-heavy SoC | Masters, queues, memories and clock domains | Dynamic-access cost, shared consumers, code/cache size, compilation time and peak memory |
| Externally clocked design | VPI off/read/full and actual observers | Dormant cost, boundary work and scoped invalidation |

The larger-design and structural matrix above is a coverage target; this
directory currently automates only the eight-lane NBA workload. A larger CPU
alone does not isolate these costs. Increasing total graph size while holding
active work fixed is particularly useful for checking pay-to-play behavior.

Measure elapsed time and hardware counters on uninstrumented binaries with
warmups and interleaved matched trials, without concurrent builds. Collect
activation, convergence and tier counters in a separate diagnostic run. The
current `run_nba8.py` measurements enable runtime diagnostics and therefore do
not establish an uninstrumented performance acceptance result. Preserve source
and executable hashes, compiler options, workload parameters and output checks.
Report compile time, peak memory and generated code size separately from
simulation throughput.

A smaller dispatcher share can hide more total work in generated helpers.
Judge a scheduling change by total instructions and elapsed time as well as
the profile breakdown. Reject repeated empty scans or extra activations even
when the apparent dispatcher hotspot shrinks. Require each optimization to
improve its predicted cost across the relevant scaling axis before extending
it to another large IP.
