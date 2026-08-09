// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s

module native_sampled_explicit_clock_dedup;
  logic main_clk = 0;
  logic alternate_clk = 0;
  logic data = 0;
  logic gate = 1;

  first: assert property (@(posedge main_clk)
      $past(data, 1, gate, @(posedge alternate_clk)) === 1'b0);
  second: assert property (@(posedge main_clk)
      $past(data, 1, gate, @(posedge alternate_clk)) === 1'b0);
endmodule

// CHECK-COUNT-1: obelisk_sim.code_unit.decl {{[0-9]+}} in 0 always hierarchy {{.*}} debug "alternate-clock sampler"
// CHECK-COUNT-1: obelisk_sim.spawn {{.*clocked_sample.*}}
// CHECK-COUNT-1: obelisk_sim.assert.clocked_sample_update
