// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/rejected.sv -o %t/rejected.mlir
// RUN: FileCheck %s --check-prefix=COHORT < %t/rejected.mlir

// COHORT: __obelisk_fused_

//--- rejected.sv
module rejected_fusions;
  bit clock;
  bit published;
  bit other;
  bit observed;
  int controlled_a;
  int controlled_b;
  string managed_a;
  string managed_b;

  always @(posedge clock)
    published = 1;

  always @(posedge clock)
    other = 1;

  always @(posedge published)
    observed = other;

  always @(posedge clock) begin : controlled_first
    controlled_a = 1;
  end

  always @(posedge clock) begin : controlled_second
    controlled_b = 1;
  end

  always @(posedge clock)
    managed_a = "a";

  always @(posedge clock)
    managed_b = "b";

  initial begin
    #1 clock = 1;
    #1;
    $finish;
  end
endmodule
