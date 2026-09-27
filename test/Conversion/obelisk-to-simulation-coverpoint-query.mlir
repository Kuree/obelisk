// RUN: %split-file %s %t
// RUN: obelisk -emit-obelisk %t/input.sv -o %t/input.mlir
// RUN: obelisk-opt %t/input.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | %python %S/Inputs/mutate-functional-batch.py add-layout \
// RUN:   > %t/lowered.mlir
// RUN: FileCheck %s < %t/lowered.mlir
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   instance-query-nonexistent < %t/lowered.mlir > %t/bad.mlir
// RUN: not obelisk-opt %t/bad.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-ITEM
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   instance-query-wrong-owner < %t/lowered.mlir > %t/bad.mlir
// RUN: not obelisk-opt %t/bad.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-ITEM
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   type-query-nonexistent < %t/lowered.mlir > %t/bad.mlir
// RUN: not obelisk-opt %t/bad.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-ITEM
// RUN: %python %S/Inputs/mutate-functional-batch.py \
// RUN:   type-query-wrong-owner < %t/lowered.mlir > %t/bad.mlir
// RUN: not obelisk-opt %t/bad.mlir \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BAD-ITEM

// IEEE 1800-2017 19.8 makes get_coverage available through both an instance
// and the static item scope. get_inst_coverage remains instance-only. All
// forms carry the same stable FunctionalItem identity into the v1 query op.
// CHECK: simulation.covergroup.instance_query {{.*}} item [[ITEM:-?[0-9]+]]
// CHECK: simulation.covergroup.type_query {{.*}} item [[ITEM]]
// CHECK: simulation.covergroup.type_query {{.*}} item [[ITEM]]
// BAD-ITEM: error: has a nonexistent or wrong-owner functional item ID

//--- input.sv
module point_query;
  bit sampled;
  covergroup cg;
    cp: coverpoint sampled {
      bins zero = {0};
      bins one = {1};
    }
  endgroup

  covergroup other;
    cp_other: coverpoint sampled { bins one = {1}; }
  endgroup

  cg cov;
  other other_cov;
  int covered;
  int total;
  real percentage;
  initial begin
    cov = new;
    other_cov = new;
    percentage = cov.cp.get_inst_coverage(covered, total);
    percentage = cov.cp.get_coverage();
    percentage = cg::cp::get_coverage(covered, total);
    percentage = other_cov.cp_other.get_inst_coverage();
    percentage = other::cp_other::get_coverage();
  end
endmodule
