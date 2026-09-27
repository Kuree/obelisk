// RUN: obelisk -emit-sim -O0 --vpi=off %s | FileCheck %s

// Keep the implementation proportional to PLA outputs, not input bits. A
// 512-input product row remains one vector operation and one reduction.
module pla_wide_lowering;
  logic [1:512] memory [1:4];
  logic [1:512] input_terms;
  logic [1:4] output_terms;
  initial $sync$and$plane(memory, input_terms, output_terms);
endmodule

module pla_async_lowering;
  logic [1:3] memory [1:2];
  logic [1:3] a, b;
  logic [1:2] output_terms;
  initial $async$or$array(memory, a ^ b, output_terms);
endmodule

// CHECK-LABEL: obelisk_sim.func private @{{.*fork.*}}(
// CHECK-SAME: schedule.detached_controls
// CHECK-SAME: schedule.prime_on_spawn
// CHECK: cf.br ^[[WAIT:[a-zA-Z0-9_]+]]
// CHECK: ^[[WAIT]]:
// CHECK: obelisk_sim.suspend.any %{{[^,]+}}, %{{[^,]+}}, %{{[^ ]+}} edges [0, 0, 0]
// CHECK-SAME: unpacked_array<1 : 2
// CHECK-SAME: packed_array<1 : 3
// CHECK-SAME: packed_array<1 : 3

// CHECK-LABEL: obelisk_sim.func private @unit_1(
// CHECK-COUNT-4: obelisk_sim.ref.subelement
// CHECK: obelisk_sim.logic.case_difference_mask
// CHECK-COUNT-4: obelisk_sim.logic.reduction and
// CHECK-NOT: obelisk_sim.logic.extract
// CHECK: obelisk_sim.ref.store
