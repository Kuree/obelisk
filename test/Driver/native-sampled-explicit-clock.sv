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

module native_sampled_explicit_clock;
  logic main_clk = 0;
  logic alternate_clk = 0;
  logic data = 0;
  logic gate = 1;
  logic [2:0] phase = 0;

  first: assert property (@(posedge main_clk)
      ((phase == 1) &&
       ($past(data, 1, gate, @(posedge alternate_clk)) === 1'bx) &&
       $rose(data, @(posedge alternate_clk)) &&
       !$fell(data, @(posedge alternate_clk)) &&
       !$stable(data, @(posedge alternate_clk)) &&
       $changed(data, @(posedge alternate_clk))) ||
      (((phase == 2) || (phase == 3)) &&
       ($past(data, 1, gate, @(posedge alternate_clk)) === 1'b1) &&
       !$rose(data, @(posedge alternate_clk)) &&
       $fell(data, @(posedge alternate_clk)) &&
       !$stable(data, @(posedge alternate_clk)) &&
       $changed(data, @(posedge alternate_clk))) ||
      ((phase == 4) &&
       ($past(data, 1, gate, @(posedge alternate_clk)) === 1'b0) &&
       !$rose(data, @(posedge alternate_clk)) &&
       $fell(data, @(posedge alternate_clk)) &&
       !$stable(data, @(posedge alternate_clk)) &&
       $changed(data, @(posedge alternate_clk))))
    $display("alternate sample pass");
  else
    $display("alternate sample FAIL");

  // A second code unit requests the identical clock/source/gate/depth tuple.
  // Planning must retain one statically spawned sampler, not advance twice.
  second: assert property (@(posedge main_clk)
      (phase != 0) |->
      ($past(data, 1, gate, @(posedge alternate_clk)) ===
       ((phase == 1) ? 1'bx : ((phase == 2 || phase == 3) ? 1'b1 : 1'b0))))
  else
    $display("dedup FAIL");

  initial begin
    #1 data = 1;
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 phase = 1;
    #1 main_clk = 1;
    #1 main_clk = 0;

    #1 data = 0;
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 phase = 2;
    #1 main_clk = 1;
    #1 main_clk = 0;

    #1 phase = 3;
    #1 main_clk = 1;
    #1 main_clk = 0;

    // The gated $past sampler ignores this edge, while the ungated
    // value-change sampler records it.
    #1 begin data = 1; gate = 0; end
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 begin data = 0; gate = 1; end
    #1 alternate_clk = 1;
    #1 alternate_clk = 0;
    #1 phase = 4;
    #1 main_clk = 1;
    #1 $finish;
  end
endmodule

// OUTPUT: alternate sample pass
// OUTPUT-NEXT: alternate sample pass
// OUTPUT-NEXT: alternate sample pass
// OUTPUT-NEXT: alternate sample pass
// OUTPUT-NOT: FAIL

// IR-COUNT-2: obelisk_sim.code_unit.decl {{[0-9]+}} in 0 always hierarchy {{.*}} debug "alternate-clock sampler"
// IR-COUNT-2: obelisk_sim.assert.clocked_sample_update
