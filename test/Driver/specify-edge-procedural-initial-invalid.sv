// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module initial_writer(input wire clock, data, output logic q);
  initial q = data;
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive specify path requires a recurring direct procedural destination writer
