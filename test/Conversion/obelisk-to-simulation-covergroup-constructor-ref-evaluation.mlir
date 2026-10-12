// RUN: %split-file %s %t
// RUN: %obelisk --std=1800-2023 -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t/lowered.mlir
// RUN: FileCheck %s --check-prefix=LIVE < %t/lowered.mlir
// RUN: obelisk-opt %t/lowered.mlir --canonicalize --cse | FileCheck %s --check-prefix=LIVE
// RUN: obelisk-opt %t/lowered.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | mlir-translate --mlir-to-llvmir | opt -S -passes=verify -o /dev/null
// RUN: obelisk-opt %t/lowered.mlir --encode-obelisk-sim-to-bytecode='vpi=off' -o /dev/null

// IEEE 19.3: a constructor ref is retained as storage until the declaration
// expressions consume it. Reads must follow the other actual's mutating call,
// including through canonicalization and CSE. The runtime receives the typed
// reference as the formal payload and current values as expression results.
// LIVE: %[[REF:[0-9]+]] = simulation.argument_ref.from_ref
// LIVE-NOT: simulation.argument_ref.load %[[REF]]
// LIVE: simulation.call
// LIVE: simulation.ref.store
// LIVE: %[[CURRENT:[0-9]+]] = simulation.argument_ref.load %[[REF]]
// LIVE: simulation.covergroup.create {{.*}}payloads[%[[REF]], {{[^]]*}}%[[CURRENT]]{{[^]]*}}] argument_count 2

//--- input.sv
module top;
  int value;
  function automatic int bump(inout int target);
    target++;
    return 0;
  endfunction
  covergroup cg(ref int live, input int ignored);
    option.auto_bin_max = live;
    cp: coverpoint value { bins selected[] = {[0:live]}; }
  endgroup
  cg cov;
  initial begin
    cov = new(value, bump(value));
  end
endmodule
