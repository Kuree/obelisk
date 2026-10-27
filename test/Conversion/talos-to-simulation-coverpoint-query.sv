// RUN: talos --std=1800-2023 -emit-talos %s | talos-opt '--lower-talos-to-sim=opt-level=0' '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.instance_query set={item = 999 : i64}' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-INSTANCE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.instance_query operand=1 poison=!simulation.covergroup_handle<@__talos_covergroup_other_1504C834F5D945B0>' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-INSTANCE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.type_query set={item = 999 : i64}' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-TYPE
// RUN: not talos-opt %t.mlir '--test-talos-edit-attributes=op=simulation.covergroup.type_query set={declaration = @__talos_covergroup_other_1504C834F5D945B0}' '--encode-talos-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-TYPE

// IEEE 1800-2023 19.8 makes get_coverage available through both an instance
// and the static item scope. get_inst_coverage remains instance-only. All
// forms carry the same stable FunctionalItem identity into the v1 query op.
// CHECK: simulation.covergroup.instance_query {{.*}} item [[ITEM:-?[0-9]+]]
// CHECK: simulation.covergroup.type_query {{.*}} item [[ITEM]]
// CHECK: simulation.covergroup.type_query {{.*}} item [[ITEM]]

// An instance query whose item ID is absent from the schema, or is owned by
// another covergroup than its handle type, is rejected at bytecode encoding.
// BAD-INSTANCE: error: has a nonexistent or wrong-owner functional item ID
// BAD-INSTANCE-NEXT: simulation.covergroup.instance_query

// The same holds for a static type query with an absent item ID or a
// declaration that does not own the item.
// BAD-TYPE: error: has a nonexistent or wrong-owner functional item ID
// BAD-TYPE-NEXT: simulation.covergroup.type_query

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
