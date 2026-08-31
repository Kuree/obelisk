// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module mixed_driver(input wire clock, data, output logic q);
  assign q = data;
  always @(posedge clock)
    q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: cannot mix continuous and procedural assignments to variable 'q'
