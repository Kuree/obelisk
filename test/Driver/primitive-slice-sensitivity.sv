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

// Every generated primitive must suspend on the selected scalar references,
// not on either complete four-bit capture.
// CHECK-COUNT-4: obelisk_sim.suspend.any {{.*}} : !obelisk_sim.ref<!obelisk_sim.logic<1>>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
// CHECK-NOT: obelisk_sim.suspend.any {{.*}}packed_array
