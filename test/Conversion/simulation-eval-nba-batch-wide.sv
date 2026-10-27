// RUN: talos -emit-slang %s | talos-opt --test-talos-native-backend='scheduler=eval timing=true' -o %t.mlir 2> %t.trace
// RUN: FileCheck %s --check-prefix=TRACE < %t.trace
// RUN: FileCheck %s < %t.mlir

// IEEE 1800-2023 4.9.4: an NBA's index and RHS are captured when it is
// enqueued. 4.5 and 10.4.2: consumers see the complete NBA barrier, and X/Z
// changes retain both planes.
// Every NBA root is private to the clocked cone, so all four commit as one
// batch: the 256-bit `lanes`, the 129-bit four-state `wide` and the `memory`
// element take the wide and captured-slot forms. Four-state commits report
// their old and new unknown planes through batch_knownness.
// TRACE: talos native NBA batch: candidates=4 roots=4 private-cone=4 unread=0 wide=3 slots=3
// CHECK-LABEL: llvm.func @__talos_aot_static_nba_commit_v1(
// CHECK-NOT: llvm.call @talos_rt_v1_static_nba_commit_ordered(
// CHECK: llvm.call @talos_rt_v1_static_nba_batch_guard(
// CHECK-NOT: llvm.call @talos_rt_v1_static_nba_commit_ordered(
// CHECK: llvm.call @talos_rt_v1_static_nba_batch_finish(
// CHECK-NOT: llvm.call @talos_rt_v1_static_nba_commit_ordered(
// CHECK: llvm.call @talos_rt_v1_static_nba_batch_knownness(
// CHECK-NOT: llvm.call @talos_rt_v1_static_nba_commit_ordered(
// CHECK: llvm.call @talos_rt_v1_static_nba_commit_roots(

module native_nba_batch_wide;
  bit clk = 0;
  always #5 clk = ~clk;
  int tick = 0;
  always @(posedge clk) tick <= tick + 1;
  bit [255:0] lanes = '0;
  logic [31:0] memory[4];
  logic [128:0] wide = '0;
  wire [255:0] lane_result = lanes ^ 256'h1357;
  wire [128:0] wide_result = wide ^ 129'h321;
  wire [31:0] mem_result[4];
  for (genvar i=0; i<4; i++) assign mem_result[i] = memory[i] ^ 32'h1234;
  always @(posedge clk) begin
    lanes[31:0] <= tick;
    lanes[127:96] <= ~tick;
    wide <= (tick % 11 == 0) ? {1'bz, 64'hxxxx, 64'h1234} : {65'h1, 64'(tick)};
  end
  always @(posedge clk)
    memory[tick % 4] <= (tick % 7 == 0) ? 32'h12xz5678 : tick;
  initial begin
    automatic string label = "NBA wide batch";
    for (int cycle=0; cycle<8; cycle++) begin
      @(posedge clk);
      #1;
      $display("%h %h %h", lane_result[31:0], mem_result[cycle % 4], wide_result);
    end
    $display("%s complete %0d", label, tick);
    $finish;
  end
endmodule
