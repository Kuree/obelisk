// RUN: obelisk-translate %s | FileCheck %s

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
