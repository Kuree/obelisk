# Eight-lane NBA scheduler microbenchmark

`nba8.sv` keeps eight edge waiters active and publishes five nonblocking
updates per lane and cycle, including a conditional same-destination
overwrite. The runner builds optimized native and compact-bytecode executables,
performs one warm-up and five measured runs at 10,000, 20,000, and 40,000
cycles. These defaults keep even the optimized native samples long enough that
process-launch and timer overhead do not dominate. The runner
checks every lane against an independent expected-value model, and emits JSON
containing median wall time, RSS, throughput, doubling ratios, and runtime
subscription counters.

Run it from the repository root:

```sh
python3 benchmark/scheduler/run_nba8.py \
  --output tmp/nba8-results.json
```

Pass `--verilator /path/to/verilator` to compile the same source with Verilator
and require identical lane output. Pass `--waiters 0 64 256` to measure scaling
with unrelated dormant global waiters. Use the generated executables with
Callgrind when instruction-level profiles are needed; `--tier native` and
`--tier bytecode` isolate the two Obelisk configurations. Design-wide
bytecode operations are included wherever the workload lowers to them; the
driver does not expose a third, independent design-task-only tier switch.
