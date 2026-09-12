// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s < %t/lowered.mlir
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   start-control-nonexistent < %t/lowered.mlir > %t/bad.mlir
// RUN: not obelisk-opt %t/bad.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-ITEM
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   stop-control-wrong-owner < %t/lowered.mlir > %t/bad.mlir
// RUN: not obelisk-opt %t/bad.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-ITEM

// IEEE 1800-2017 Table 19-5 permits start() and stop() on coverpoints. The
// instance control op carries the selected template FunctionalItem ID; zero
// remains the existing whole-covergroup selector.
// CHECK: obelisk_sim.covergroup.stop {{.*}} item [[FIRST:-?[0-9]+]]
// CHECK: obelisk_sim.covergroup.start {{.*}} item [[FIRST]]
// CHECK: obelisk_sim.covergroup.stop {{.*}} item [[SECOND:-?[0-9]+]]
// CHECK: obelisk_sim.covergroup.start {{.*}} item [[SECOND]]
// BAD-ITEM: error: has a nonexistent or wrong-owner functional item ID

//--- input.sv
module coverpoint_control;
  bit sampled;

  covergroup first;
    cp: coverpoint sampled { bins values[] = {0, 1}; }
  endgroup

  covergroup second;
    cp: coverpoint sampled { bins values[] = {0, 1}; }
  endgroup

  first a;
  second b;
  initial begin
    a = new;
    b = new;
    a.cp.stop();
    a.cp.start();
    b.cp.stop();
    b.cp.start();
  end
endmodule
