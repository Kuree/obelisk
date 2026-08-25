// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module derived_control(input wire clock, enable, data, output logic q);
  always @(posedge (clock & enable))
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive procedural specify path requires a direct single-source event control
