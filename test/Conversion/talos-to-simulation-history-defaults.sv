// RUN: talos -emit-slang %s | talos-opt --convert-slang-to-talos --lower-talos-to-sim=opt-level=0 \
// RUN:   | FileCheck %s --check-prefix=SNAPSHOT

// LRM 16.5.1 and 16.9.3: before-history values come from declaration defaults
// and recursive expression evaluation. A two-state expression can default to
// one; a four-state expression with X operands can still default to zero.
// Gated depth-two history and a distinct alternate clock track actual samples.
// A side-effecting initializer executes once per declaration, never on history
// underflow. Only the declaration whose history is used receives a snapshot.
// SNAPSHOT-COUNT-2: debug "__talos_sample_default"
// SNAPSHOT-NOT: debug "__talos_sample_default"
// The side-effecting initializer runs once and is snapshotted only for the
// declaration whose history is read.
// SNAPSHOT-LABEL: simulation.func private @unit_6(
// SNAPSHOT: %[[INIT:.*]] = simulation.call @unit_5(%arg0)
// SNAPSHOT: simulation.ref.store %[[INIT]] to
// SNAPSHOT: %[[SNAPSHOT:.*]] = simulation.context.storage %arg0[10]
// SNAPSHOT-NEXT: simulation.ref.store %[[INIT]] to %[[SNAPSHOT]]
// SNAPSHOT-LABEL: simulation.func private @unit_7(
// SNAPSHOT-NOT: simulation.context.storage %arg0[10]
// SNAPSHOT: simulation.return
// Procedural $past evaluates its expression over each static variable's
// sample from a prior clock tick, which a sampler process records. The gated
// depth-two sampler records only clock edges that the gate qualifies.
// SNAPSHOT: simulation.suspend.edge_iff posedge %arg2 iff %arg3
// SNAPSHOT: simulation.assert.clocked_sample_update %arg0 from %{{.*}} gate %{{.*}} id [[GATED:[0-9]+]] depth 2
// On history underflow, $past(zero + 1) adds one to the type default that
// fills zero's sample ring, $past(initialized) uses the declaration
// initializer 4'b0101, and $past(dynamic_value) reads the snapshot instead of
// calling initialize().
// SNAPSHOT-LABEL: simulation.func private @unit_11(
// SNAPSHOT-NOT: simulation.call @unit_5
// SNAPSHOT: %[[ZERO:.*]] = simulation.assert.clocked_sample_read %arg0 id {{[0-9]+}} depth 1 age 0 : (!simulation.context) -> !simulation.packed_array<3 : 0 x i1>
// SNAPSHOT-NEXT: %[[ZERO_BITS:.*]] = simulation.packed.flatten %[[ZERO]]
// SNAPSHOT-NEXT: %[[ONE:.*]] = arith.constant {simulation.rematerialized} 1 : i4
// SNAPSHOT-NEXT: arith.addi %[[ZERO_BITS]], %[[ONE]] : i4
// SNAPSHOT: %[[INITIALIZED_VALID:.*]] = simulation.assert.clocked_sample_read %arg0 id {{[0-9]+}} depth 1 age 0 : (!simulation.context) -> i1
// SNAPSHOT-NEXT: cf.cond_br %[[INITIALIZED_VALID]], ^{{bb[0-9]+}}(%{{.*}}), ^[[DECLARED:bb[0-9]+]]
// SNAPSHOT-NEXT: ^[[DECLARED]]:
// SNAPSHOT-NEXT: simulation.logic.constant 5 : i4, 0 : i4
// SNAPSHOT: %[[DYNAMIC_VALID:.*]] = simulation.assert.clocked_sample_read %arg0 id {{[0-9]+}} depth 1 age 0 : (!simulation.context) -> i1
// SNAPSHOT-NEXT: cf.cond_br %[[DYNAMIC_VALID]], ^{{bb[0-9]+}}(%{{.*}}), ^[[UNDERFLOW:bb[0-9]+]]
// SNAPSHOT-NEXT: ^[[UNDERFLOW]]:
// SNAPSHOT-NEXT: %[[SLOT:.*]] = simulation.context.storage %arg0[10]
// SNAPSHOT-NEXT: simulation.ref.load %[[SLOT]]
// SNAPSHOT: simulation.assert.clocked_sample_read %arg0 id [[GATED]] depth 2 age 1 :
// SNAPSHOT-LABEL: simulation.func private @unit_12

`timescale 1ns / 1ps
module history_default_matrix;
  bit clk = 0;
  bit [3:0] zero;
  logic [3:0] initialized = 5;
  logic flag = 1;
  bit gate = 1;
  int calls = 0;
  function automatic logic [3:0] initialize();
    calls++;
    return 9;
  endfunction
  logic [3:0] dynamic_value = initialize();
  logic [3:0] unused_value = initialize();
  logic [3:0] expected = 5;
  logic [3:0] lanes[0:1] = '{4'b1001, 4'bxxxx};
  always #1 clk = ~clk;
  always @(posedge clk)
    $display("defaults %h %h %h calls=%0d gate=%h nested=%b masked=%h lane=%h",
             $past(zero + 4'b1), $past(initialized), $past(dynamic_value), calls,
             $past(initialized, 2, gate), $fell($past(flag)),
             $past(4'bxxxx & 4'b0), $past(lanes[0]));
  assert property (@(posedge clk)
                   $past(initialized, 2, , @(negedge clk)) === expected)
    $display("alternate %h", $sampled(expected));
  else $fatal(1, "alternate default mismatch");
  initial begin
    initialized = 7;
    dynamic_value = 3;
    flag = 0;
    lanes[0] = 3;
    lanes[1] = 4;
    #2 initialized = 8;
    gate = 0;
    dynamic_value = 'x;
    #2 initialized = 9;
    gate = 1;
    expected = 7;
    dynamic_value = 'z;
    #2 initialized = 10;
    expected = 8;
    #2 $finish;
  end
endmodule
