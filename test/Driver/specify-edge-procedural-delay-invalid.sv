// RUN: obelisk -emit-sim %s -o - | FileCheck %s
module delayed(input wire clock, data, output logic q);
  always @(posedge clock)
    #1 q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: simulation.time.now
// CHECK: simulation.ref.store_inertial_path
