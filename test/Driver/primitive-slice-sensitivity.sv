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

// The coalesced primitive kernel must suspend on all eight selected scalar
// references, not on either complete four-bit capture.
// CHECK: simulation.suspend.any {{.*}} : [[SCALAR:!simulation.ref<!simulation.logic<1>>]], [[SCALAR]], [[SCALAR]], [[SCALAR]], [[SCALAR]], [[SCALAR]], [[SCALAR]], [[SCALAR]], i1
// CHECK-NOT: simulation.suspend.any {{.*}}packed_array
