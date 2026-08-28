// RUN: not obelisk -emit-sim %s -o - 2>&1 | FileCheck %s
module derived_control(input wire clock, enable, data, output logic q);
  always @(posedge (clock & enable))
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: edge-sensitive procedural specify path source is not observed by a direct event control or implicit sensitivity
