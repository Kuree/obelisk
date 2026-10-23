// RUN: obelisk -O3 --vpi=off %s -o %t.eval
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out 2>&1
// RUN: %t.eval > %t.eval.out 2>&1 && diff -u %t.reference.out %t.eval.out
// RUN: FileCheck %s < %t.reference.out

// `sel` is never initialized, so the first posedge reads X (IEEE 1800-2023
// 6.8, Table 6-7) and `sel === 2'bxx` selects 0 (11.4.5). The always_ff body
// has a path-guarded two-state copy whose activation-local probe proves the
// bits it reads known; it has no promotion certificate. A ranked sweep chose
// that copy by certificate alone and read X as 0, starting one step ahead.
module native_eval_ranked_path_guarded_initial_x;
  logic clk;
  logic [1:0] sel;
  logic [3:0] y;
  always_comb
    unique case (sel)
      2'd0: y = 4'd1;
      2'd1: y = 4'd2;
      2'd2: y = 4'd4;
    endcase
  initial begin
    clk = 1'b0;
    forever #5 clk = ~clk;
  end
  always_ff @(posedge clk)
    sel <= (sel === 2'bxx || sel == 2'd2) ? 2'd0 : sel + 2'd1;
  initial begin
    repeat (4) @(y) $display("%0t y=%0d sel=%0d", $time, y, sel);
    $finish;
  end
endmodule

// CHECK: 5 y=1 sel=0
// CHECK-NEXT: 15 y=2 sel=1
// CHECK-NEXT: 25 y=4 sel=2
// CHECK-NEXT: 35 y=1 sel=0
