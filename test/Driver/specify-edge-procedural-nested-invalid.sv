// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module nested(input wire clock, gate, data, output logic q);
  always @(posedge clock) begin
    @(posedge gate);
    q = data;
  end
  specify
    (posedge clock => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive procedural specify path requires one outer wake point
