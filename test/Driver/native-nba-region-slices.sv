// RUN: obelisk -O3 --native-scheduler=eval %s -o %t.eval
// RUN: obelisk -O0 --native-scheduler=eval %s -o %t.o0
// RUN: obelisk -O3 --native-scheduler=generic %s -o %t.generic
// RUN: obelisk -O0 --execution-tier=bytecode %s -o %t.bytecode
// RUN: %t.eval | FileCheck %s
// RUN: %t.o0 | FileCheck %s
// RUN: %t.generic | FileCheck %s
// RUN: %t.bytecode | FileCheck %s

// LRM 4.6/4.9.4: forwarding may collapse private repeated slices, but must
// preserve X/Z, conditional writes, another process's disjoint write, and
// source order for overlapping targets.
module native_nba_region_slices;
  bit clk = 0;
  always #5 clk = ~clk;
  int tick = 0;
  logic [23:0] slices = 0;
  logic [15:0] overlap = 0;
  always @(posedge clk) begin
    tick <= tick + 1;
    slices[7:0] <= 8'(~tick);
    slices[15:8] <= 8'(tick + 3);
    if (tick & 1) begin
      slices[7:0] <= 8'hxz;
      slices[15:8] <= 8'(tick + 9);
    end else begin
      slices[7:0] <= 8'(tick + 1);
    end
    overlap <= 16'h1234;
    overlap[7:0] <= 8'h56;
    overlap[11:4] <= 8'hab;
  end
  always @(posedge clk) slices[23:16] <= 8'(tick + 42);
  initial begin
    for (int n = 0; n < 20; ++n) begin
      @(posedge clk);
      if (tick != n) $fatal(1, "early NBA");
      #1;
      if (slices[7:0] !== ((n & 1) ? 8'hxz : 8'(n + 1)) ||
          slices[15:8] !== 8'(n + ((n & 1) ? 9 : 3)) ||
          slices[23:16] !== 8'(n + 42) || overlap !== 16'h1ab6)
        $fatal(1, "slice NBA order or mask");
    end
    $display("slice NBA PASS");
    $finish;
  end
endmodule
// CHECK: slice NBA PASS
