// RUN: %obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' | obelisk-opt --pass-pipeline='builtin.module(obelisk-sim-prepare{prune-unused-coverage=true},simulation.design(symbol-dce),obelisk-sim-prepare-coverage,simulation.design(obelisk-sim-devirtualize-class-calls),obelisk-sim-prepare-unit-lowering,simulation.design(cse),simulation.design(simulation.func(obelisk-sim-lower-unit,canonicalize,cse,mem2reg,canonicalize,cse,obelisk-sim-mark-bounded-loops,obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),obelisk-sim-finalize)' -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir
// RUN: obelisk-opt %t.sim.mlir '--encode-obelisk-sim-to-bytecode=vpi=off' --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | %llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --seed=13 | FileCheck %s --check-prefix=RUNTIME
// REQUIRES: compiled-sampler

// IEEE 1800-2023 18.3, 18.5 and 18.8: pure shared arithmetic keeps the
// complete relation, soft preferences, live captures and frozen properties.
// Each of six diamonds computes (e + e) - e modulo 256 and uses its operand
// three times; common subexpression elimination before constraint lowering
// leaves one shared graph. Expanding it as a tree would need 3^6 leaf
// occurrences, but the encoded program contains 46 instructions because each
// shared value is stored once and reloaded at its other uses. The duplicate
// hard predicate and its soft copy reuse one value across roots.
// PLAN: simulation.random.solve
// PLAN-SAME: "ODR1\01\00\18\00\10\00\00\00.\00\00\00
// RUNTIME: shared template valid 1

// One diamond computes (e + e) - e modulo 256, which is e again, using the
// value e three times.
`define DIAMOND(e) (((e) + (e)) - (e))
`define CHAIN(e) `DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(`DIAMOND(e))))))

class C;
  rand bit [7:0] x, y;
  bit [7:0] limit = 8'd10;
  constraint legal {
    `CHAIN(x + y) >= limit;
    `CHAIN(x + y) >= limit;
    soft `CHAIN(x + y) >= limit;
  }
  constraint preferred { soft `CHAIN(x + y) < 8'd40; }
endclass

module top;
  C c;
  int valid;
  int low;
  bit [7:0] kept_x, kept_y;
  initial begin
    c = new;
    valid = 1;
    // limit 10: the later soft preference (< 40) is satisfiable.
    repeat (100) begin
      valid &= c.randomize();
      valid &= 8'(c.x + c.y) >= 8'd10 && 8'(c.x + c.y) < 8'd40;
    end
    // Live capture: limit 100 makes the preference unsatisfiable, so only
    // the hard bound remains.
    c.limit = 8'd100;
    repeat (100) begin
      valid &= c.randomize();
      valid &= 8'(c.x + c.y) >= 8'd100;
    end
    // A frozen property is a capture for the other candidate.
    c.y.rand_mode(0);
    c.y = 8'd250;
    c.limit = 8'd10;
    repeat (100) begin
      valid &= c.randomize();
      valid &= c.y == 8'd250 && 8'(c.x + c.y) >= 8'd10 && 8'(c.x + c.y) < 8'd40;
    end
    // Failure keeps every frozen field.
    c.x.rand_mode(0);
    c.x = 8'd1;
    c.y = 8'd2;
    c.limit = 8'd200;
    valid &= c.randomize() == 0;
    valid &= c.x == 8'd1 && c.y == 8'd2;
    $display("shared template valid %0d", valid);
  end
endmodule
