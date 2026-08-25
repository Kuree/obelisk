// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module mixed_driver(input wire clock, data, output logic q);
  assign q = data;
  always @(posedge clock)
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive specify path destination mixes continuous and procedural writers
