# Real-UVM smoke benchmark

This benchmark compiles the full Accellera UVM package together with a minimal
registered `uvm_test`, then executes the generated simulator and verifies that
the run phase completed without UVM errors or fatals. It is deliberately not a
lit test: its purpose is reproducible compile-time and runtime measurement.

From the repository root:

```sh
python3 benchmark/uvm/run.py
```

The default is `-O3 --execution-tier=bytecode -fno-lto`, the fast production
configuration for library-heavy UVM designs. Use `--execution-tier=native` to
measure native lowering, `--lto` to include full runtime LTO, and
`--keep-binary PATH` to retain the generated executable. The UVM source tree
defaults to the CMake-fetched checkout and can be overridden with `--uvm-root`.
Compilation is capped at 60 seconds by default, matching the performance gate;
override it with `--compile-timeout` for diagnostic native measurements.
