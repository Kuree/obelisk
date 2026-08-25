// RUN: obelisk -O3 --vpi=off -emit-sim %s -o - | FileCheck %s

module primitive_slice_sensitivity;
  logic [3:0] data;
  logic [3:0] control;
  wire [3:0] result;

  genvar i;
  generate
    for (i = 0; i < 4; ++i)
      nmos n0(result[i], data[i], control[i]);
  endgenerate
endmodule

// Native primitive cohort fusion combines the four actors into one union
// wait, but every watched operand must still be the selected scalar reference
// rather than either complete four-bit capture.
// CHECK: obelisk_sim.suspend.any
// CHECK-SAME: edges [0, 0, 0, 0, 0, 0, 0, 0]
// CHECK-SAME: : !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-NOT: obelisk_sim.suspend.any {{.*}}packed_array
