// RUN: obelisk-translate %s | obelisk-opt --convert-moore-to-obelisk \
// RUN:   | obelisk-opt | FileCheck %s

module net_lvalues(
  output wire [15:0] x,
  input  wire [3:0]  a
);
  assign x[3:0] = a;
  assign {x[15:12], x[11:8]} = {a, a};
endmodule

// CHECK-LABEL: obelisk.semantic.graph_symbol @net_lvalues module
// CHECK: obelisk.net.extract
// CHECK: obelisk.net.concat
// CHECK: obelisk.semantic.effect assign
// CHECK-NOT: moore.
