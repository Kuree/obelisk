// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos '--test-talos-set-module-attributes=attrs="{llvm.data_layout = \"e-m:e-p:64:64-i64:64-n8:16:32:64-S128\", llvm.target_triple = \"x86_64-unknown-linux-gnu\"}"' -o %t.talos.mlir
// RUN: talos-opt %t.talos.mlir --lower-talos-to-sim=opt-level=0 -o %t.sim.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.sim.mlir

// IEEE 1800-2023 16.5.1, 16.9.3: $sampled(data+1), $past(data+1),
// and $fell($past(flag)) evaluate sampled operands recursively. The clocked
// process changes data and index before evaluating these expressions, while
// the ordinary adjacent display operand must still see the current data.
// The dynamic selection uses the Preponed index, not its newly assigned 1.
// $past(data+1) adds one to the sample of data from the prior clock tick,
// which a sampler process records at every tick.
// This is semantic MLIR input; no SystemVerilog frontend is needed by the test.
// PLAN: simulation.func private @unit_3.$clocked_sample.[[SITE:[0-9]+]](
// PLAN: simulation.suspend.edge negedge
// PLAN-SAME: resume_region = 16
// PLAN: simulation.assert.sampled_read
// PLAN: simulation.assert.clocked_sample_update {{.*}} id [[SITE]] depth 1
// PLAN: %[[DATA:.*]] = simulation.assert.sampled_read {{.*}} from %arg2
// PLAN: %[[FLAT:.*]] = simulation.packed.flatten %[[DATA]]
// PLAN: %[[SUM:.*]] = simulation.logic.binary add %[[FLAT]], {{.*}}
// PLAN: simulation.ref.load %arg2
// PLAN: %[[INDEX:.*]] = simulation.assert.sampled_read {{.*}} from %arg4
// PLAN-NEXT: %[[INDEX_BITS:.*]] = simulation.packed.flatten %[[INDEX]]
// PLAN-NEXT: %[[SELECTOR:.*]] = simulation.logic.resize %[[INDEX_BITS]]
// PLAN-NEXT: simulation.array.extract_dynamic %[[DATA]][%[[SELECTOR]]]
// PLAN: %[[PRIOR:.*]] = simulation.assert.clocked_sample_read {{.*}} id [[SITE]] depth 1 age 0
// PLAN-NEXT: %[[PRIOR_BITS:.*]] = simulation.packed.flatten %[[PRIOR]]
// PLAN: simulation.logic.binary add %[[PRIOR_BITS]], {{.*}}
// PLAN: %[[FLAG:.*]] = simulation.assert.sampled_read {{.*}} from %arg3
// PLAN: %[[PAST:.*]] = simulation.assert.sampled_history {{.*}} from %[[FLAG]]
// PLAN: simulation.assert.sampled_history {{.*}} from %[[PAST]]

`timescale 1ns / 1ps
module sampled_expression;
  logic clk = 0;
  logic [3:0] data;
  logic flag;
  logic [1:0] index = 0;
  always #1 clk = ~clk;
  always @(negedge clk) begin
    data = (data + 2);
    index = 1;
    $display("sample=%h live=%h pick=%b past=%h nested=%b", $sampled((data + 1'b1)), data, $sampled(data[index]), $past((data + 1'b1)), $fell($past(flag)));
  end
  initial begin
    #1 data = 3;
    flag = 1;
    index = 0;
    #2 data = 5;
    flag = 0;
    index = 0;
    #2 data = 7;
    flag = 1;
    index = 0;
    #2 data = 9;
    flag = 0;
    index = 0;
    #2 $finish;
  end
endmodule
