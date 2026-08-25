// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module comb_lhs_only(input wire source, data, output logic q);
  always_comb begin
    force source = data;
    q = data;
  end
  specify
    (posedge source => (q +: data)) = 2;
  endspecify
endmodule
// CHECK: error: edge-sensitive procedural specify path source is not observed by a direct event control or implicit sensitivity
