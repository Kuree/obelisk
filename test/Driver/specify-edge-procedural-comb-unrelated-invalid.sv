// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module comb_unrelated(input wire clock, data, output logic q);
  always_comb
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive procedural specify path source is not observed by a direct event control or implicit sensitivity
