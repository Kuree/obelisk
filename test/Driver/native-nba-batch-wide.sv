// RUN: obelisk -O3 --native-scheduler=eval --mlir-timing %s -o %t.eval 2> %t.plan
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.eval 2> %t.diag | FileCheck %s
// RUN: FileCheck %s --check-prefix=PLAN < %t.plan
// RUN: FileCheck %s --check-prefix=DIAG < %t.diag

// LRM 4.9.4: index and RHS are captured when enqueued. LRM 4.5/10.4.2:
// consumers see the complete NBA barrier, and X/Z changes retain both planes.
module native_nba_batch_wide;
  bit clk = 0;
  always #5 clk = ~clk;
  int tick = 0;
  always @(posedge clk) tick <= tick + 1;
  bit [255:0] lanes = '0;
  logic [31:0] memory[32];
  logic [128:0] wide = '0;
  wire [255:0] lane_result = lanes ^ 256'h1357;
  wire [128:0] wide_result = wide ^ 129'h321;
  wire [31:0] mem_result[32];
  for (genvar i=0; i<32; i++) assign mem_result[i] = memory[i] ^ 32'h1234;
  always @(posedge clk) begin
    lanes[31:0] <= tick;
    lanes[127:96] <= ~tick;
    wide <= (tick % 11 == 0) ? {1'bz, 64'hxxxx, 64'h1234} : {65'h1, 64'(tick)};
  end
  always @(posedge clk)
    memory[tick % 32] <= (tick % 7 == 0) ? 32'h12xz5678 : tick;
  initial begin
    automatic string label = "NBA wide batch";
    for (int i=0; i<32; i++) memory[i] = '0;
    for (int cycle=0; cycle<5000; cycle++) begin
      @(posedge clk);
      if (tick != cycle) $fatal(1, "early commit");
      #1;
      if ((lane_result[31:0] ^ 32'h1357) !== 32'(cycle) ||
          lane_result[127:96] !== ~32'(cycle) || lane_result[255:128] !== 128'b0)
        $fatal(1, "partial wide update");
      if (memory[cycle % 32] !==
          ((cycle % 7 == 0) ? 32'h12xz5678 : 32'(cycle))) $fatal(1, "slot capture");
      if (mem_result[cycle % 32] !==
          (((cycle % 7 == 0) ? 32'h12xz5678 : 32'(cycle)) ^ 32'h1234))
        $fatal(1, "slot consumer");
      if (wide_result !==
          (((cycle % 11 == 0) ? {1'bz, 64'hxxxx, 64'h1234} : {65'h1, 64'(cycle)}) ^ 129'h321))
        $fatal(1, "wide X/Z");
    end
    $display("%s complete %0d", label, tick);
    $finish;
  end
endmodule
// CHECK: NBA wide batch complete 5000
// PLAN: obelisk native NBA batch: candidates=
// PLAN-SAME: wide={{[1-9][0-9]*}} slots={{[1-9][0-9]*}}
// DIAG: aot_nba_batches={{[1-9][0-9]*}} aot_nba_private_commits={{[1-9][0-9]*}}
