// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/fused-ssa.sv -o %t/fused-ssa.mlir
// RUN: FileCheck %s --check-prefix=FUSED-SSA < %t/fused-ssa.mlir
// RUN: obelisk -O3 --vpi=read -emit-sim %t/fused-ssa.sv -o %t/fused-ssa-read.mlir
// RUN: FileCheck %s --check-prefix=READ-FUSED-SSA < %t/fused-ssa-read.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-fused-ssa.test.

// FUSED-SSA: obelisk_sim.func private @__obelisk_fused_
// FUSED-SSA-NOT: obelisk_sim.termination.requested
// FUSED-SSA-NOT: obelisk_sim.ref.store
// FUSED-SSA: arith.select
// FUSED-SSA-NOT: obelisk_sim.termination.requested
// FUSED-SSA-NOT: obelisk_sim.ref.store
// FUSED-SSA: arith.select
// FUSED-SSA-NOT: obelisk_sim.termination.requested
// FUSED-SSA-NOT: obelisk_sim.ref.store
// READ-FUSED-SSA: obelisk_sim.func private @__obelisk_fused_
// READ-FUSED-SSA-COUNT-2: obelisk_sim.ref.store
// READ-FUSED-SSA-NOT: obelisk.eval.discardable_store

//--- fused-ssa.sv
module fused_ssa;
  bit clock;
  bit reset = 1;
  bit [31:0] value [0:1];

  function automatic bit [31:0] mix(input bit [31:0] input_value);
    mix = input_value ^ (input_value << 7) ^ (input_value >> 11);
  endfunction

  for (genvar lane = 0; lane < 2; lane++) begin : lanes
    always @(posedge clock) begin
      bit [31:0] next;
      if (reset) begin
        value[lane] <= lane;
      end else begin
        next = mix(value[lane] + lane + 1);
        value[lane] <= next;
        if (next[0])
          value[lane] <= next ^ 32'ha5a5_0000;
      end
    end
  end

  initial begin
    #1 clock = 1;
    #1 clock = 0;
    reset = 0;
    repeat (4) begin
      #1 clock = 1;
      #1 clock = 0;
    end
    #1;
    $display("%08h %08h", value[0], value[1]);
    $finish;
  end
endmodule
