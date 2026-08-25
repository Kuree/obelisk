// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module delayed(input wire clock, data, output logic q);
  always @(posedge clock)
    #1 q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive specify path has a delayed procedural destination dependency
