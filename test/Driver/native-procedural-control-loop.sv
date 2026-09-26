// RUN: obelisk -O3 --vpi=off --mlir-timing %s -o %t.native 2>%t.log
// RUN: FileCheck %s --check-prefix=ADMIT < %t.log
// RUN: %t.native | FileCheck %s
// RUN: obelisk -O0 --vpi=off %s -o %t.o0
// RUN: %t.o0 | FileCheck %s
// RUN: obelisk -O3 --vpi=off --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.bytecode | FileCheck %s

// A while-loop whose trip count depends on the data has no bounded-induction
// proof, but it never suspends: each activation runs it to completion (IEEE
// 1800-2023 4.7, 9.2.2.2). The compiled activation keeps the loop native and
// must agree with the interpreter on every result.
// ADMIT: obelisk native eligibility: eligible=1 fully_eligible=1
// ADMIT-NOT: control-loop group requires bytecode scheduling

// CHECK: width(00000001)=1
// CHECK: width(00000080)=8
// CHECK: width(00010000)=17
// CHECK: width(00000000)=0

module native_procedural_control_loop;
  logic clk = 0;
  logic [31:0] value = 0;
  int width;

  always_comb begin
    logic [31:0] rest;
    rest = value;
    width = 0;
    while (rest != 0) begin
      rest = rest >> 1;
      width = width + 1;
    end
  end

  always #5 clk = ~clk;

  initial begin
    @(posedge clk) value = 32'h1;
    @(negedge clk) $display("width(%h)=%0d", value, width);
    @(posedge clk) value = 32'h80;
    @(negedge clk) $display("width(%h)=%0d", value, width);
    @(posedge clk) value = 32'h10000;
    @(negedge clk) $display("width(%h)=%0d", value, width);
    @(posedge clk) value = 32'h0;
    @(negedge clk) $display("width(%h)=%0d", value, width);
    $finish;
  end
endmodule
