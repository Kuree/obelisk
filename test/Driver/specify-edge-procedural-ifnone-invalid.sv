// RUN: not obelisk -emit-sim %s -o /dev/null 2>&1 | FileCheck %s
module procedural_ifnone(input wire clock, data, output logic q);
  always @(posedge clock)
    q = data;
  specify
    ifnone (clock => q) = 2;
  endspecify
endmodule
// Clause 30.4.4.4 makes ifnone a simple-path declaration. Direct procedural
// simple paths remain outside this edge-sensitive tranche.
// CHECK: error: simple specify path output has no continuous driver
