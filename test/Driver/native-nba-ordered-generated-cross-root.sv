// RUN: obelisk -O3 --native-scheduler=auto -emit-llvm %s -o %t.ll
// RUN: FileCheck %s --check-prefix=LLVM < %t.ll
// RUN: obelisk -O3 -fno-lto --native-scheduler=auto %s -o %t.auto
// RUN: obelisk -O3 -fno-lto --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.auto.out

// Generated Eval must publish interleaved updates to both roots in enqueue
// order, including intermediate edges hidden by each root's final value.
// Cross-root order itself is visible only to an event expression spanning
// both roots (LRM 9.4.2), and such an expression keeps a design off
// generated Eval today. native-nba-ordered-cross-root.sv checks that order
// on the runtime path; this test pins the generated queue's per-root order.
module native_nba_ordered_generated_cross_root;
  logic clk = 0;
  logic [31:0] a = 0;
  logic [31:0] b = 0;
  int a_edges = 0;
  int b_edges = 0;

  always #5 clk = ~clk;
  always @(posedge a[0]) a_edges = a_edges + 1;
  always @(posedge b[0]) b_edges = b_edges + 1;
  always @(posedge clk) begin
    a[0] <= 1;
    b[0] <= 1;
    a[0] <= 0;
    b[0] <= 0;
    for (int j = 0; j < 81; j = j + 1) begin
      a[8] <= j & 1;
      b[8] <= j & 1;
    end
    a[31:24] <= a[31:24] + 1;
    b[31:24] <= b[31:24] + 1;
  end
  initial begin
    #36;
    $display("a=%0d b=%0d ae=%0d be=%0d", a[31:24], b[31:24],
             a_edges, b_edges);
    $finish;
  end
endmodule

// LLVM: @__obelisk_eval_ordered_nba_queue_v1 = internal global
// LLVM: define i32 @__obelisk_eval_dispatch_v1
// OUTPUT: a=4 b=4 ae=4 be=4
