// RUN: obelisk -emit-sim %s -o - | FileCheck %s
module delayed(input wire clock, data, output logic q);
  always @(posedge clock)
    #1 q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: obelisk_sim.time.now
// CHECK: obelisk_sim.ref.store_inertial_path
