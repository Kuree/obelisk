// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module dynamic_select(input wire clock, data, input int index,
                      output logic [3:0] q);
  always @(posedge clock)
    q[index] = data;
  specify
    (posedge clock *> (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: procedural timing path requires an in-range fixed selection
