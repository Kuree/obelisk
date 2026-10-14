// RUN: obelisk --std=1800-2023 -emit-obelisk %s | obelisk-opt '--lower-obelisk-to-sim=opt-level=0' '--test-obelisk-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.start set={item = 999 : i64}' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-ITEM
// RUN: not obelisk-opt %t.mlir '--test-obelisk-edit-attributes=op=simulation.covergroup.stop operand=1 poison=!simulation.covergroup_handle<@__obelisk_covergroup_second_446212831502C8F0>' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null 2>&1 | FileCheck %s --check-prefix=BAD-OWNER

// IEEE 1800-2023 Table 19-5 permits start() and stop() on coverpoints. The
// instance control op carries the selected template FunctionalItem ID; zero
// remains the existing whole-covergroup selector.
// CHECK: simulation.covergroup.stop {{.*}} item [[FIRST:-?[0-9]+]]
// CHECK: simulation.covergroup.start {{.*}} item [[FIRST]]
// CHECK: simulation.covergroup.stop {{.*}} item [[SECOND:-?[0-9]+]]
// CHECK: simulation.covergroup.start {{.*}} item [[SECOND]]

// An item ID absent from the schema is rejected at bytecode encoding.
// BAD-ITEM: error: has a nonexistent or wrong-owner functional item ID
// BAD-ITEM-NEXT: simulation.covergroup.start

// Stopping the first coverpoint through a handle of the second covergroup
// names an item owned by another covergroup, which is rejected.
// BAD-OWNER: error: has a nonexistent or wrong-owner functional item ID
// BAD-OWNER-NEXT: simulation.covergroup.stop

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
