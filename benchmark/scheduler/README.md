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
