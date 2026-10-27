// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir 2>&1 | FileCheck %s --check-prefix=TRACE --allow-empty
// RUN: FileCheck %s < %t.mlir

// A two-state packed vector (`bit [7:0]`) has no unknown plane, so it cannot
// constrain the path-known predicate of a checkpoint owner (IEEE 1800-2023
// 6.11.2). Only the four-state register is probed; the owner keeps its
// generated two-state route and forced generated eval stays enabled.
// TRACE-NOT: talos eval probe rejected
// TRACE-NOT: unguarded runtime leaf
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32, native.debug_timing
} {
  simulation.design @two_state_probe {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "top.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "top.lane"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.packed_array<7 : 0 x i1> design
    simulation.storage.decl 2 in 0 : !simulation.packed_array<7 : 0 x i1> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %clock = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %lane = simulation.spawn @lane(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @lane(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %source = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.packed_array<7 : 0 x i1>>
      %copy = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.packed_array<7 : 0 x i1>>
      %count = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %bits = simulation.ref.load %source : !simulation.ref<!simulation.packed_array<7 : 0 x i1>> -> !simulation.packed_array<7 : 0 x i1>
      %old = simulation.ref.load %count : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next = simulation.logic.binary add %old, %one : !simulation.logic<8>
      simulation.nba.enqueue %bits to %copy : (!simulation.packed_array<7 : 0 x i1>, !simulation.ref<!simulation.packed_array<7 : 0 x i1>>) -> ()
      simulation.nba.enqueue %next to %count : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %limit = simulation.logic.constant 255 : i8, 0 : i8 : !simulation.logic<8>
      %done = simulation.logic.compare case_eq %old, %limit : (!simulation.logic<8>, !simulation.logic<8>) -> i1
      cf.cond_br %done, ^fatal, ^wait
    ^fatal:
      %zero = arith.constant 0 : i32
      simulation.fatal %ctx, %zero
      simulation.return
    }
  }
}

// CHECK-DAG: llvm.func @lane.__talos_eval_body_0.__talos_path_known
// CHECK-DAG: llvm.func @lane.__talos_eval_body_0.__talos_two_state
