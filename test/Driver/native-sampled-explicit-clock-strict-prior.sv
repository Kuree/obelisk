// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.generic-o0
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=aot %s -o %t.aot-o3
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.generic-o3 > %t.generic-o3.out
// RUN: %t.aot-o3 > %t.aot-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: diff -u %t.generic-o0.out %t.generic-o3.out
// RUN: diff -u %t.generic-o0.out %t.aot-o3.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o0.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic-o0.out
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=IR

module native_sampled_explicit_clock_strict_prior;
  logic main_clk = 0;
  logic alternate_clk = 0;
  logic data = 0;
  logic [2:0] phase = 0;

  strict_prior: assert property (@(posedge main_clk)
      ((phase == 1) &&
       ($past(data, 1, , @(posedge alternate_clk)) === 1'bx) &&
       ($past(data, 2, , @(posedge alternate_clk)) === 1'bx) &&
       !$rose(data, @(posedge alternate_clk)) &&
       $fell(data, @(posedge alternate_clk)) &&
       !$stable(data, @(posedge alternate_clk)) &&
       $changed(data, @(posedge alternate_clk))) ||
      (((phase == 2) || (phase == 3)) &&
       ($past(data, 1, , @(posedge alternate_clk)) === 1'b0) &&
       ($past(data, 2, , @(posedge alternate_clk)) === 1'bx) &&
       $rose(data, @(posedge alternate_clk)) &&
       !$fell(data, @(posedge alternate_clk)) &&
       !$stable(data, @(posedge alternate_clk)) &&
       $changed(data, @(posedge alternate_clk))) ||
      ((phase == 4) &&
       ($past(data, 1, , @(posedge alternate_clk)) === 1'b1) &&
       ($past(data, 2, , @(posedge alternate_clk)) === 1'b0) &&
       !$rose(data, @(posedge alternate_clk)) &&
       !$fell(data, @(posedge alternate_clk)) &&
       $stable(data, @(posedge alternate_clk)) &&
       !$changed(data, @(posedge alternate_clk))))
    $display("strict prior pass");
  else
    $display("strict prior FAIL");

  initial begin
    #1 phase = 1;
    // Both edges occur in one time step. The alternate-clock sample from this
    // slot is not a strictly prior sample while the assertion is in Observed.
    // The caller-current sampled value of data is still its Preponed zero.
    #1 begin data = 1; alternate_clk = 1; main_clk = 1; end
    #1 begin alternate_clk = 0; main_clk = 0; end

    // No alternate edge: both evaluations compare the new caller-current one
    // against the same most recent strictly-prior alternate sample, zero.
    #1 phase = 2;
    #1 main_clk = 1;
    #1 main_clk = 0;
    #1 phase = 3;
    #1 main_clk = 1;
    #1 main_clk = 0;

    // Add a second alternate sample in its own time step. Depth two now names
    // the first sample, while depth one and the change functions name this one.
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 phase = 4;
    #1 main_clk = 1;
    #1 $finish;
  end
endmodule

// OUTPUT-COUNT-4: strict prior pass
// OUTPUT-NOT: FAIL

// IR-COUNT-2: obelisk_sim.code_unit.decl {{[0-9]+}} in 0 always hierarchy {{.*}} debug "alternate-clock sampler"
// IR: obelisk_sim.suspend.edge posedge
// IR-SAME: resume_region = 16 : i32
// IR: obelisk_sim.assert.clocked_sample_update
