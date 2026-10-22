// RUN: obelisk -O3 --mlir-timing %s -o %t.eval 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O0 --native-scheduler=generic %s -o %t.reference
// RUN: %t.reference > %t.reference.out 2>&1
// RUN: %t.eval > %t.eval.out 2>&1 && diff -u %t.reference.out %t.eval.out
// RUN: FileCheck %s < %t.reference.out

// The deferred violation report (IEEE 1800-2023 12.4.2.1) makes this body a
// path-guarded owner. `s` is X until the first edge, so the time-0 activation
// matches no item and leaves `y` at its initial 'x (6.8, Table 6-7). Once `s`
// is known, its two-state copy writes only the value plane of `y`; the path
// guard must send the activation that still finds X in `y` to the four-state
// body, which clears the unknown plane.
module native_eval_path_guarded_write_target;
  logic clk = 0;
  logic [5:0] s;
  logic [7:0] n = 0;
  logic [5:0] y;
  always #5 clk = ~clk;
  always_comb begin
    unique casez (s)
      6'b0?????: y = 1;
      6'b1?????: y = 2;
    endcase
  end
  always_ff @(posedge clk) begin
    n <= n + 1;
    s <= n[0] ? 6'd33 : 6'd5;
  end
  initial begin
    #6 $display("t6 s=%b y=%b", s, y);
    #10 $display("t16 s=%b y=%b", s, y);
    $finish;
  end
endmodule

// TIER: obelisk eval executor inventory: {{.*}} runtime_checkpoints=0
// CHECK: :21:5: warning: unique case violation: no match
// CHECK: t6 s=000101 y=000001
// CHECK: t16 s=100001 y=000010
