// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module event_list(input wire clock, reset, data, output logic q);
  always @(posedge clock or posedge reset)
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive procedural specify path requires a direct single-source event control
