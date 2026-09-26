// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/fused-callee-write.sv -o %t/fused-callee-write.mlir
// RUN: FileCheck %s --check-prefix=CALLEE-WRITE-IR < %t/fused-callee-write.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-fused-callee-write.test.

// CALLEE-WRITE-IR: obelisk_sim.func private @__obelisk_fused_

//--- fused-callee-write.sv
module fused_callee_write;
  bit clock;
  bit gate = 1;
  bit value [2];

  function automatic void clear_gate();
    gate = 0;
  endfunction

  for (genvar lane = 0; lane < 2; ++lane) begin
    always @(posedge clock) begin
      if (gate)
        value[lane] <= 1;
      else
        value[lane] <= 0;
      if (lane == 0)
        clear_gate();
    end
  end

  initial begin
    clock = 0;
    #1 clock = 1;
    #1 $display("%0d %0d", value[0], value[1]);
    $finish;
  end
endmodule
