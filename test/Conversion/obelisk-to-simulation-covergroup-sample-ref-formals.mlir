// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=LIVE < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=LIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | obelisk-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 13.5.2 and 19.8.1: ref reads follow storage changes made by another
// actual. Input values retain their chosen argument-time snapshot. The
// reference remains typed through lowering; memory effects preserve the load
// after the mutating call through canonicalization and CSE.
// LIVE: %[[FROZEN:[0-9]+]] = simulation.ref.load %[[STORAGE:[^ ]+]] : !simulation.ref<i32> -> i32
// LIVE: %[[REF:[0-9]+]] = simulation.argument_ref.from_ref %[[STORAGE]]
// LIVE-NOT: simulation.argument_ref.load %[[REF]]
// LIVE: simulation.call
// LIVE: simulation.ref.store
// LIVE: %[[CURRENT:[0-9]+]] = simulation.argument_ref.load %[[REF]]
// LIVE: %[[SUM:[0-9]+]] = arith.addi %[[FROZEN]], %[[CURRENT]]
// LIVE: %[[PACKED:[0-9]+]] = simulation.logic.from_bits %[[SUM]]
// LIVE: simulation.covergroup.sample {{.*}} values[%[[PACKED]]]

//--- input.sv
module top;
  int value;
  function automatic int bump(inout int target);
    target++;
    return 0;
  endfunction
  covergroup cg with function sample(input int frozen, ref int live,
                                     input int ignored);
    cp: coverpoint frozen + live;
  endgroup
  cg cov;
  initial begin
    cov = new;
    value = 1;
    cov.sample(value, value, bump(value));
  end
endmodule
