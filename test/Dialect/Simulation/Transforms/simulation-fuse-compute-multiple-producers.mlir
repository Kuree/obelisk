// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/multiple-producers.sv -o %t/multiple-producers.mlir
// RUN: FileCheck %s --check-prefix=MULTIPLE-PRODUCERS < %t/multiple-producers.mlir

// MULTIPLE-PRODUCERS: simulation.design
// MULTIPLE-PRODUCERS-NOT: schedule.static_fusion
// MULTIPLE-PRODUCERS: __obelisk_fused_

//--- multiple-producers.sv
module multiple_clock_producers;
  bit clock;
  int first;
  int second;

  always @(posedge clock)
    first = 1;

  initial
    #1 clock = 1;

  initial
    #1 clock = 0;

  always @(posedge clock)
    second = 1;

  initial begin
    #2;
    $finish;
  end
endmodule
