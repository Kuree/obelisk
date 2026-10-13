// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off --native-scheduler=eval -emit-llvm %s -o - | FileCheck %s --check-prefix=LIBRARY
// RUN: obelisk --clocked-kernels=immediate -O3 --vpi=off --native-scheduler=eval --mlir-timing %s -o %t.o3 2> %t.timing
// RUN: FileCheck %s --check-prefix=PLAN < %t.timing
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.o3 > %t.o3.out 2> %t.o3.diag
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.o3.out
// RUN: FileCheck %s --check-prefix=COMMITS < %t.o3.diag
// RUN: %t.o3 --execution-tier=bytecode | diff -u %t.o3.out -

// LRM 4.9.3/4.9.4, 10.4.2: reads see current state while captured pending
// updates remain private. The equal second capture overwrites the different
// first one; fixed partial writes preserve the blocking update to other bits.
// X/Z captures exercise the barrier's independent knownness decision.
module native_eval_local_pending;
  bit clk = 0;
  int count = 0;
  logic [15:0] q = 16'h120x;
  logic [7:0] x = 8'h33;
  always #5 clk = ~clk;
  always @(posedge clk) count <= count + 1;
  always @(posedge clk) begin
    q[15:8] = q[15:8] + 1;
    if (count != 4) begin
      q[7:4] <= ~q[7:4];
      q[7:4] <= q[7:4];
    end
    if (count == 1) x <= 'x;
    else if (count == 3) x <= 'z;
    else x <= count;
  end
  initial begin
    automatic string label = "local";
    repeat (6) begin
      @(posedge clk);
      $display("active %0d %h", count, q[7:4]);
      #1;
      $display("%s %0d %h %h", label, count, q, x);
    end
    $finish;
  end
endmodule

// PLAN: obelisk eval local pending roots: 3 cores={{[1-9][0-9]*}} fixed-candidates=3
// LRM 4.6(b)/38.36.1: even one edge-triggered source statement can capture
// repeatedly before a barrier. Full requires an execution bound in addition
// to a dormant-observer guard; retain canonical staging without that bound.
// FULL-PLAN: obelisk eval transaction source roots: 0/3
// All 17 staged root updates are committed by their private consumers,
// including q's final equal-value overwrite. The runtime must not consume them
// again after a logical dirty word crosses a CFG join (LRM 4.9.4/10.4.2).
// COMMITS: aot_nba_commits=17
// COMMITS-SAME: aot_nba_private_commits=17
// A separately linked library does not prove a closed execution environment.
// LIBRARY-NOT: .__obelisk_pending
// OUTPUT: active 0 0
// OUTPUT-NEXT: local 1 130x 00
// OUTPUT-NEXT: active 1 0
// OUTPUT-NEXT: local 2 140x xx
// OUTPUT-NEXT: active 2 0
// OUTPUT-NEXT: local 3 150x 02
// OUTPUT-NEXT: active 3 0
// OUTPUT-NEXT: local 4 160x zz
// OUTPUT-NEXT: active 4 0
// OUTPUT-NEXT: local 5 170x 04
// OUTPUT-NEXT: active 5 0
// OUTPUT-NEXT: local 6 180x 05
