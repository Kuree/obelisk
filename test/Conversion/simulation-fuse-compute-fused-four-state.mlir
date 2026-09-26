// RUN: %split-file %s %t
// RUN: obelisk -O3 -emit-sim %t/fused-four-state.sv -o %t/fused-four-state.mlir
// RUN: FileCheck %s --check-prefix=FOUR-STATE < %t/fused-four-state.mlir

// Runtime behavior is checked in ../Runtime/simulation-fuse-compute-fused-four-state.test.

// FOUR-STATE: obelisk_sim.func private @__obelisk_fused_
// FOUR-STATE-NOT: obelisk_sim.ref.store
// FOUR-STATE: obelisk_sim.aggregate.extract
// FOUR-STATE: obelisk_sim.logic.is_true
// FOUR-STATE: arith.select
// FOUR-STATE: obelisk_sim.nba.enqueue
// FOUR-STATE-NOT: obelisk_sim.ref.store
// FOUR-STATE: obelisk_sim.aggregate.extract
// FOUR-STATE: obelisk_sim.logic.is_true
// FOUR-STATE: arith.select
// FOUR-STATE: obelisk_sim.nba.enqueue
// FOUR-STATE-NOT: obelisk_sim.ref.store

//--- fused-four-state.sv
module fused_four_state;
  bit clock;
  bit reset = 1;
  logic [31:0] value [2];

  function automatic logic [31:0] mix(logic [31:0] input_value);
    mix = input_value ^ (input_value << 7) ^ (input_value >> 11);
  endfunction

  for (genvar lane = 0; lane < 2; ++lane) begin
    always @(posedge clock) begin
      logic [31:0] next;
      if (reset)
        value[lane] <= lane;
      else begin
        next = mix(value[lane] + lane + 1);
        value[lane] <= next;
        if (next[0])
          value[lane] <= next ^ 32'ha5a5_0000;
      end
    end
  end

  initial begin
    clock = 0;
    reset = 0;
    repeat (4) begin
      #1 clock = 1;
      #1 clock = 0;
    end
    #1 $display("%08h %08h", value[0], value[1]);
    $finish;
  end
endmodule
