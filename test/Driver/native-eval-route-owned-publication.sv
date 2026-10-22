// RUN: obelisk -O3 --vpi=off %s -o %t.eval
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out 2>&1
// RUN: %t.eval > %t.eval.out 2>&1 && diff -u %t.reference.out %t.eval.out
// RUN: FileCheck %s < %t.reference.out

// The deferred violation report (IEEE 1800-2023 12.4.2.1) makes the
// always_comb a checkpointed route: its runtime report leaves are cut from the
// two-state and four-state route bodies and run once in the checkpoint body.
// `y` is also watched by the runtime-owned initial loop, so each route body
// publishes its store of `y` through the owned transition bridge (4.6(a),
// 9.4.2). That call belongs to the eval call closure and must stay in the route
// bodies; cutting it like a report leaf turned every change of `y` into an
// invalid-design failure instead of waking the loop.
module native_eval_route_owned_publication;
  logic clk = 1'b0;
  logic [1:0] sel;
  logic [3:0] y;
  always_comb
    unique case (sel)
      2'd0: y = 4'd1;
      2'd1: y = 4'd2;
      2'd2: y = 4'd4;
    endcase
  always #5 clk = ~clk;
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
