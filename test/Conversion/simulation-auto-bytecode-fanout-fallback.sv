// RUN: obelisk -emit-slang %s | obelisk-opt --test-obelisk-native-backend='scheduler=auto timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s --implicit-check-not='llvm.call @obelisk_rt_v1_scheduler_static_transition' < %t.mlir

// A bytecode fanout owner makes Auto discard its partial eval island. Direct
// state stores must then publish transitions through the generic scheduler.
// The clocked process forks a new process on each edge (IEEE 1800-2023 9.3.2
// join_none); a spawn without static multiplicity keeps its block in
// bytecode.
// TRACE: partial eval disabled: bytecode fanout owner actor=2 continuation=12
// CHECK: llvm.call @obelisk_rt_v1_scheduler_signal_transition(

module native_partial_auto_fallback_bytecode_fanout;
  logic clk = 0;
  logic en = 1;
  logic [7:0] b = 0;
  logic [31:0] wide = 0;
  logic [31:0] mix [0:2];
  always #5 clk = ~clk;
  assign mix[0] = wide;
  for (genvar g = 0; g < 2; g++) begin : heavy
    assign mix[g + 1] = (mix[g] << 1) ^ (mix[g] >> 3) ^ (32'h9e3779b9 + g);
  end
  always @(posedge clk) begin
    b <= 8'd1;
    if (en) b <= 8'd0;
    fork wide[15:8] <= wide[15:8] + 1; join_none
    wide[31:24] <= wide[31:24] + 1;
  end
  initial begin
    automatic string label = "done";
    #48 $display("%s b=%0d n=%0d mix=%h", label, b, wide[31:24], mix[2]);
    $finish;
  end
endmodule
