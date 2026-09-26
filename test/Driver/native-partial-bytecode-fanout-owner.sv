// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.auto > %t.auto.out
// RUN: %t.reference > %t.reference.out
// RUN: diff -u %t.reference.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// Each clock edge forks a new process (IEEE 1800-2023 9.3.2 join_none), so the
// spawn has no static multiplicity and the clocked process keeps its spawning
// block in bytecode. The partial eval island then has no generated executor
// for that continuation. Auto must decline the island before packed lowering
// emits static NBA staging; the late owner check could only report "partial
// eval ownership failed after static NBA lowering".
module native_partial_bytecode_fanout_owner;
  logic clk = 0;
  logic en = 1;
  logic [7:0] b = 0;
  logic [31:0] wide = 0;
  int b_changes = 0;

  always #5 clk = ~clk;
  always @(b) b_changes = b_changes + 1;
  always @(posedge clk) begin
    b <= 8'd1;
    if (en) b <= 8'd0;
    fork wide[15:8] <= wide[15:8] + 1; join_none
    wide[31:24] <= wide[31:24] + 1;
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
