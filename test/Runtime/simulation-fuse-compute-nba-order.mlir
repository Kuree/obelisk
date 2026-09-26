// RUN: %split-file %s %t
// RUN: obelisk -O0 --native-scheduler=generic %t/nba-order.sv -o %t/nba-order.unfused
// RUN: obelisk -O3 --native-scheduler=generic %t/nba-order.sv -o %t/nba-order.fused
// RUN: %t/nba-order.unfused > %t/nba-order.unfused.out
// RUN: %t/nba-order.fused > %t/nba-order.fused.out
// RUN: diff %t/nba-order.unfused.out %t/nba-order.fused.out
// RUN: FileCheck %s --check-prefix=NBA-ORDER < %t/nba-order.fused.out

// IEEE 1800-2023 4.6-4.8: these samples race with other Active processes.
// Check permitted values and required intra-process ordering, not the old
// scheduler's cross-process interleaving. The cohorts retain publications.
// NBA-ORDER: 2

//--- nba-order.sv
module same_root_nba_order;
  bit clock;
  int value;

  always @(posedge clock)
    value <= 1;

  always @(posedge clock)
    value <= 2;

  initial begin
    #1 clock = 1;
    #1;
    $display("%0d", value);
    $finish;
  end
endmodule
