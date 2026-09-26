// RUN: obelisk -O3 --native-scheduler=auto --mlir-timing -emit-llvm %s -o %t.ll 2> %t.diag
// RUN: FileCheck %s --check-prefix=PROOF < %t.diag
// RUN: FileCheck %s --check-prefix=IR --implicit-check-not=obelisk_rt_v1_scheduler_static_transition < %t.ll
// RUN: obelisk -O3 -fno-lto --native-scheduler=auto %s -o %t.auto
// RUN: %t.auto +MEM=%t.mem | FileCheck %s --check-prefix=OUTPUT

// A bytecode fanout owner makes Auto discard its partial eval island. Direct
// state stores must then publish transitions through the generic scheduler.
module native_partial_auto_fallback_bytecode_fanout;
  logic clk = 0;
  logic en = 1;
  int limit = 81;
  logic [7:0] b = 0;
  logic [31:0] wide = 0;
  logic [31:0] ram [0:3];
  logic [1:0] address = 0;
  logic [31:0] fetched;
  logic [31:0] mix [0:63];
  int fetch_changes = 0;
  string path;
  int fd;

  always #5 clk = ~clk;
  always_comb fetched = ram[address];
  assign mix[0] = fetched ^ wide;
  for (genvar g = 0; g < 63; g++) begin : heavy
    assign mix[g + 1] = (mix[g] << 1) ^ (mix[g] >> 3) ^ (32'h9e3779b9 + g);
  end
  always @(fetched) fetch_changes++;
  always @(posedge clk) begin
    b <= 8'd1;
    if (en) b <= 8'd0;
    for (int j = 0; j < limit; j++)
      wide[15:8] <= j & 1;
    wide[31:24] <= wide[31:24] + 1;
  end

  initial begin
    if (!$value$plusargs("MEM=%s", path)) $fatal(1, "missing MEM");
    fd = $fopen(path, "w");
    $fwrite(fd, "12345679 00000000 00000000 00000000\n");
    $fclose(fd);
    #1;
    $readmemh(path, ram);
    #12 limit = 82;
    #36;
    $display("fetched=%h changes=%0d b=%0d n=%0d mix=%h", fetched,
             fetch_changes, b, wide[31:24], mix[63]);
    $finish;
  end
endmodule

// PROOF: partial eval disabled: bytecode fanout owner
// IR: @obelisk_rt_v1_scheduler_signal_transition
// OUTPUT: fetched=12345679 changes=1 b=0 n=5 mix=96c8ad26
