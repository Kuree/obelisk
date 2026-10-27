// RUN: %split-file %s %t
// RUN: talos -O3 --vpi=off --mlir-timing %t/logic.sv -o %t/logic 2> %t/logic.plan
// RUN: FileCheck %s --check-prefix=PLAN < %t/logic.plan
// RUN: %t/logic | FileCheck %s --check-prefix=LOGIC
// RUN: talos -O3 --vpi=off --mlir-timing %t/bit.sv -o %t/bit 2> %t/bit.plan
// RUN: FileCheck %s --check-prefix=PLAN < %t/bit.plan
// RUN: %t/bit | FileCheck %s --check-prefix=BIT

// A checker on the negative edge of its clock port prints that port, as the
// basejump memory address checks do. The clocked kernel admits it and reads
// the clock source instead of the port copy: in the reference algorithm the
// checker runs after the copy's update event, when the copy equals the source
// (IEEE 1800-2023 23.3.3, 4.9.1), so the printed level is the post-edge 0.
// A 2-state source feeding a 4-state port is read through its exact widening.
// PLAN: talos clocked kernel: {{.*}} clock_reads=1

// The 4-state port copy takes `clk_i` from x to 0 at time zero, which is a
// negedge as well (Table 9-2), with either source.
// LOGIC: neg 1 clk_i=0 q=0
// LOGIC-NEXT: neg 2 clk_i=0 q=1
// LOGIC-NEXT: neg 3 clk_i=0 q=2
// LOGIC-NEXT: neg 4 clk_i=0 q=3
// LOGIC-NOT: clk_i=1
// BIT: neg 1 clk_i=0 q=0
// BIT-NEXT: neg 2 clk_i=0 q=1
// BIT-NEXT: neg 3 clk_i=0 q=2
// BIT-NEXT: neg 4 clk_i=0 q=3
// BIT-NOT: clk_i=1

//--- leaf.sv
module leaf(input logic clk_i, input logic [7:0] q_i);
  int count;
  always_ff @(negedge clk_i) begin
    count <= count + 1;
    $display("neg %0d clk_i=%b q=%0d", count + 1, clk_i, q_i);
  end
endmodule

//--- logic.sv
`include "leaf.sv"
module top;
  logic clk = 0;
  logic [7:0] q = 0;
  always #5 clk = ~clk;
  always_ff @(posedge clk) q <= q + 1;
  leaf u(.clk_i(clk), .q_i(q));
  initial begin
    #36 $finish;
  end
endmodule

//--- bit.sv
`include "leaf.sv"
module top;
  bit clk = 0;
  logic [7:0] q = 0;
  always #5 clk = ~clk;
  always_ff @(posedge clk) q <= q + 1;
  leaf u(.clk_i(clk), .q_i(q));
  initial begin
    #36 $finish;
  end
endmodule
