// RUN: obelisk -O0 -fno-lto --native-scheduler=generic %s -o %t.reference
// RUN: obelisk -O3 -fno-lto --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: %t.reference > %t.reference.out
// RUN: %t.auto --execution-tier=native > %t.native.out
// RUN: %t.auto --execution-tier=bytecode > %t.bytecode.out
// RUN: diff -u %t.reference.out %t.native.out
// RUN: diff -u %t.reference.out %t.bytecode.out
// RUN: FileCheck %s < %t.native.out

// Roots watched only by waits for any change merge their NBAs, but a bit
// rewritten with a different value in one barrier must still wake those
// waits (IEEE 1800-2017 4.6(b), 9.4.2); an equal rewrite must not.
//  - `a` round-trips before `always @*` ever ran, so ya becomes a + 1 = 1.
//  - `b` round-trips every cycle; its counter sees one change per cycle.
//  - `c` is rewritten with its current value; its counter stays zero.
//  - `d[3:0]` round-trips; a wait on d[7:0] sees it, one on d[15:8] not.
module native_nba_change_watch_transient;
  logic clk = 0;
  logic [7:0] a = 0;
  logic [7:0] ya = 8'd5;
  logic [7:0] b = 0;
  logic [7:0] c = 8'd7;
  logic [15:0] d = 0;
  logic [31:0] wide = 0;
  int b_changes = 0;
  int c_changes = 0;
  int low_changes = 0;
  int high_changes = 0;

  always #5 clk = ~clk;
  always @* ya = a + 8'd1;
  always @(b) b_changes = b_changes + 1;
  always @(c) c_changes = c_changes + 1;
  always @(d[7:0]) low_changes = low_changes + 1;
  always @(d[15:8]) high_changes = high_changes + 1;

  always @(posedge clk) begin
    a <= 8'd3;
    a <= 8'd0;
    b <= 8'd1;
    b <= 8'd0;
    c <= 8'd7;
    c <= 8'd7;
    d[3:0] <= 4'h5;
    d[3:0] <= 4'h0;
    for (int j = 0; j < 81; j = j + 1)
      wide[15:8] <= j & 1;
    wide[31:24] <= wide[31:24] + 1;
  end

  initial begin
    #36;
    $display("ya=%0d b=%0d b_changes=%0d c_changes=%0d low=%0d high=%0d n=%0d",
             ya, b, b_changes, c_changes, low_changes, high_changes,
             wide[31:24]);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: ya=1 b=0 b_changes=4 c_changes=0 low=4 high=0 n=4
