// RUN: obelisk -emit-sim %s -o - | FileCheck %s
module event_list(input wire clock, reset, data, output logic q);
  always @(posedge clock or posedge reset)
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: simulation.ref.store_inertial_path
