// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module unrelated_control(input wire clock, data, output logic q);
  always @(posedge clock)
    q = data;
  specify
    (posedge data => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive procedural specify path source is not observed by a direct event control or implicit sensitivity
