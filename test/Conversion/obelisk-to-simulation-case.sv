// RUN: obelisk -emit-sim %s | FileCheck %s

// The importer emits every case label expression before every case item body,
// and slang orders the default body last regardless of where it is written.
// This checks that label groups stay bound to the right item when `default`
// comes first in source and one item carries two labels.

module case_shapes;
  logic [1:0] selector;
  logic [7:0] result;
  always_comb begin
    case (selector)
      default: result = 8'hff;
      2'd0: result = 8'h10;
      2'd1, 2'd2: result = 8'h20;
    endcase
  end
endmodule

// CHECK-LABEL: obelisk_sim.func {{.*}}@unit_0
// CHECK: %[[SEL_AGG:.*]] = obelisk_sim.ref.load %arg1
// CHECK: %[[SEL:.*]] = obelisk_sim.packed.flatten %[[SEL_AGG]]

// First item: one label, matching 2'd0, storing 8'h10.
// Constant labels and values may be folded and carried as continuation block
// arguments.
// CHECK: %[[EQ0:.*]] = obelisk_sim.logic.compare case_eq %[[SEL]], %{{.*}}
// CHECK: cf.cond_br %[[EQ0]], ^[[ITEM0:.*]], ^[[NEXT0:.*]]
// CHECK: ^[[ITEM0]]:
// CHECK: obelisk_sim.ref.store %{{.*}} to %arg2

// Second item: two labels combined with a disjunction, storing 8'h20.
// CHECK: ^[[NEXT0]]:
// CHECK: %[[EQ1:.*]] = obelisk_sim.logic.compare case_eq %[[SEL]]
// CHECK: %[[EQ2:.*]] = obelisk_sim.logic.compare case_eq %[[SEL]]
// CHECK: %[[ANY:.*]] = arith.ori %[[EQ1]], %[[EQ2]]
// CHECK: cf.cond_br %[[ANY]], ^[[ITEM1:.*]], ^[[NEXT1:.*]]
// CHECK: ^[[ITEM1]]:
// CHECK: obelisk_sim.ref.store

// The default body is the unconditional fall-through, with no further compare.
// CHECK: ^[[NEXT1]]:
// CHECK-NOT: obelisk_sim.logic.compare
// CHECK: obelisk_sim.ref.store
