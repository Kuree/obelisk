// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' | obelisk-opt --pass-pipeline='builtin.module(obelisk-sim-prepare{prune-unused-coverage=true},simulation.design(symbol-dce),obelisk-sim-prepare-coverage,simulation.design(obelisk-sim-devirtualize-class-calls),obelisk-sim-prepare-unit-lowering,simulation.design(cse),simulation.design(simulation.func(obelisk-sim-lower-unit,obelisk-sim-compile-random-plans,canonicalize,cse,mem2reg,canonicalize,cse,obelisk-sim-mark-bounded-loops,obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),obelisk-sim-finalize)' -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3, 18.5 and 18.8: the shared graph is 130 bits wide,
// with six diamonds and a zero literal reused at every diamond. Values above
// 2^129 survive both compiler Z3 decoders and the runtime plan. A wide
// literal is serialized once, so its word pool does not grow per use: the
// program has 63 instructions and nine literal words, three for each of the
// 130-bit constants 0, 4 and 10. The repeated hard predicate and its soft
// copy share one value across roots; live capture changes and failure
// retention of frozen properties are checked at run time.
// PLAN: simulation.random.solve_wide
// PLAN-SAME: "ODR1\02\00 \00\04\01\00\00?\00\00\00\01\00\00\001\02\00\00\09\00\00\00
// RUNTIME: shared wide template valid 1

// One diamond computes (e + e) - e, which is e again, and adds the 130-bit
// zero literal that every diamond reuses.
`define DIAMOND(e) (((((e) + (e)) - (e))) + 130'd0)
`define CHAIN(e) `DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(e))))))

class W;
  rand bit [129:0] x, y;
  bit [129:0] limit = 130'd3;
  constraint legal {
    `CHAIN(x + y) >= 130'd10;
    `CHAIN(x + y) >= 130'd10;
    soft `CHAIN(x + y) >= 130'd10;
    `CHAIN(x + y) != 130'd4;
  }
  constraint preferred { soft `CHAIN(x + y) > limit; }
endclass

module top;
  W w;
  int valid;
  bit [129:0] kept_x, kept_y;
  initial begin
    w = new;
    valid = 1;
    repeat (100) begin
      valid &= w.randomize();
      valid &= 130'(w.x + w.y) >= 130'd10 && 130'(w.x + w.y) != 130'd4;
    end
    // A live capture above 2^129 selects only values with the top bit set.
    w.limit = 130'h2_0000_0000_0000_0000_0000_0000_0000_0001;
    repeat (100) begin
      valid &= w.randomize();
      valid &= 130'(w.x + w.y) > w.limit;
    end
    // Frozen properties are captures; failure keeps them.
    w.x.rand_mode(0);
    w.y.rand_mode(0);
    w.x = 130'd1;
    w.y = 130'd2;
    valid &= w.randomize() == 0;
    valid &= w.x == 130'd1 && w.y == 130'd2;
    w.x = 130'd2;
    w.y = 130'd2;
    w.limit = 130'd3;
    valid &= w.randomize() == 0;
    $display("shared wide template valid %0d", valid);
  end
endmodule
