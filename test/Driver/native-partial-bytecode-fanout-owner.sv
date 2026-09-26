// RUN: obelisk -O3 -fno-lto --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O0 -fno-lto --native-scheduler=generic %s -o %t.reference
// RUN: %t.auto > %t.auto.out
// RUN: %t.reference > %t.reference.out
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// The loop bound changes after startup, so the loop is a scheduler-owned
// control loop and the clocked process keeps a bytecode block. The partial eval island then
// has no generated executor for that continuation. Auto must decline the
// island before packed lowering emits static NBA staging; the late owner check
// could only report "partial eval ownership failed after static NBA lowering".
module native_partial_bytecode_fanout_owner;
  logic clk = 0;
  logic en = 1;
  int limit = 81;
  logic [7:0] b = 0;
  logic [31:0] wide = 0;
  int b_changes = 0;

  always #5 clk = ~clk;
  always @(b) b_changes = b_changes + 1;
  always @(posedge clk) begin
    b <= 8'd1;
    if (en) b <= 8'd0;
    for (int j = 0; j < limit; j = j + 1)
      wide[15:8] <= j & 1;
    wide[31:24] <= wide[31:24] + 1;
  end

  initial begin
    #12 limit = 82;
  end

  initial begin
    #36;
    $display("b=%0d b_changes=%0d n=%0d", b, b_changes, wide[31:24]);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 fully_eligible=0 cost_effective=1
// TIER: partial eval disabled: bytecode fanout owner
// CHECK: b=0 b_changes=4 n=4
