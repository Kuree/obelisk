// RUN: obelisk --std=1800-2023 -O0 --native-scheduler=generic %s -o %t.generic-o0
// RUN: obelisk --std=1800-2023 -O3 --native-scheduler=generic %s -o %t.generic-o3
// RUN: obelisk --std=1800-2023 -O0 --execution-tier=bytecode %s -o %t.bytecode-o0
// RUN: obelisk --std=1800-2023 -O3 --execution-tier=bytecode %s -o %t.bytecode-o3
// RUN: %t.generic-o0 > %t.generic-o0.out
// RUN: %t.generic-o3 > %t.generic-o3.out
// RUN: %t.bytecode-o0 > %t.bytecode-o0.out
// RUN: %t.bytecode-o3 > %t.bytecode-o3.out
// RUN: diff -u %t.generic-o0.out %t.generic-o3.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o0.out
// RUN: diff -u %t.generic-o0.out %t.bytecode-o3.out
// RUN: FileCheck %s --check-prefix=OUTPUT < %t.generic-o0.out
// RUN: obelisk --std=1800-2023 -O0 -emit-sim %s | FileCheck %s --check-prefix=IR

module native_sampled_explicit_clock_condition;
  logic main_clk = 0;
  logic alternate_clk = 0;
  logic data = 0;
  logic gate = 0;
  logic qualify = 0;

  event_condition: assert property (@(posedge main_clk)
      ($past(data, 1, gate, @(posedge alternate_clk)) === 1'b1) &&
      !$rose(data, @(posedge alternate_clk iff qualify)) &&
      $fell(data, @(posedge alternate_clk iff qualify)) &&
      !$stable(data, @(posedge alternate_clk iff qualify)) &&
      $changed(data, @(posedge alternate_clk iff qualify)))
    $display("event condition pass");
  else
    $display("event condition FAIL");

  initial begin
    #1 data = 1;
    // Both conditions are true at the primary edge and become false later in
    // the same Active process. An exact edge-iff wait must latch this event.
    #1 begin
      gate = 1;
      qualify = 1;
      alternate_clk = 1;
      gate = 0;
      qualify = 0;
    end
    #1 alternate_clk = 0;

    #1 data = 0;
    // Both conditions are false at the primary edge and become true later in
    // the same Active process. This occurrence must remain rejected.
    #1 begin
      alternate_clk = 1;
      gate = 1;
      qualify = 1;
    end
    #1 alternate_clk = 0;

    #1 main_clk = 1;
    #1 $finish;
  end
endmodule

// OUTPUT: event condition pass
// OUTPUT-NOT: FAIL

// IR-COUNT-2: obelisk_sim.code_unit.decl {{[0-9]+}} in 0 always hierarchy {{.*}} debug "alternate-clock sampler"
// IR: obelisk_sim.suspend.edge_iff posedge
// IR-SAME: resume_region = 16 : i32
// IR: obelisk_sim.assert.clocked_sample_update
