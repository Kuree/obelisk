// RUN: obelisk-translate %s | FileCheck %s
// RUN: obelisk-translate %s | obelisk-opt --convert-moore-to-obelisk \
// RUN:   | obelisk-opt | FileCheck %s --check-prefix=OBELISK

module counter(
  input logic clk,
  input logic rst,
  output logic [7:0] count
);
  always_ff @(posedge clk) begin
    if (rst)
      count <= '0;
    else
      count <= count + 1'b1;
  end
endmodule

// CHECK-LABEL: moore.module @counter
// CHECK: moore.procedure always_ff
// CHECK: moore.wait_event
// CHECK: moore.detect_event posedge
// CHECK: moore.nonblocking_assign

// OBELISK-LABEL: obelisk.semantic.graph_symbol @counter module
// OBELISK: !obelisk.logic<1>
// OBELISK: obelisk.net.alloc wire
// OBELISK: obelisk.var.alloc
// OBELISK: obelisk.net.read
// OBELISK: obelisk.logic.to_bits
// OBELISK: obelisk.logic.constant 0 : i8, 0 : i8
// OBELISK: obelisk.load
// OBELISK: obelisk.logic.binary add
// OBELISK: obelisk.nba.enqueue
// OBELISK-NOT: moore.
