// RUN: obelisk -O3 -fno-lto --native-scheduler=auto --mlir-timing %s -o %t.auto 2> %t.timing
// RUN: FileCheck %s --check-prefix=TIER < %t.timing
// RUN: obelisk -O3 -fno-lto --native-scheduler=generic %s -o %t.generic
// RUN: %t.auto > %t.auto.out
// RUN: %t.generic > %t.generic.out
// RUN: diff -u %t.generic.out %t.auto.out
// RUN: FileCheck %s < %t.auto.out

// The event suspension reenters this one NBA statement twice before the NBA
// region. The intervening #0 moves the trigger write to Inactive; both NBA
// updates are then performed in the order they were scheduled.
module native_nba_ordered_reentry;
  logic trigger = 0;
  logic [31:0] wide = 0;
  int edges = 0;

  always @(posedge wide[0]) edges = edges + 1;
  always @(trigger) wide[0] <= trigger;

  initial begin
    #1 trigger = 1;
    #0 trigger = 0;
    #1;
    $display("q=%b edges=%0d", wide[0], edges);
    $finish;
  end
endmodule

// TIER: native eligibility: eligible=1 {{.*}} cost_effective=1
// CHECK: q=0 edges=1
