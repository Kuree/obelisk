// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=PROBE
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=WRITE

// A reset path does not read either old register. Its early entry proof is
// activation-local; the persistent selector still covers both registers and
// is established only after their canonical NBA writes have committed.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32,
  obelisk.native.closed_executable
} {
  simulation.design @reset_promotion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "reset.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "reset.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "reset.lane"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<1> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
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
      %reset = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<1>>
      %q = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %r = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %asserted = simulation.ref.load %reset : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %take_reset = simulation.logic.is_true %asserted : !simulation.logic<1>
      cf.cond_br %take_reset, ^reset, ^step
    ^reset:
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      cf.br ^publish(%zero, %zero : !simulation.logic<8>, !simulation.logic<8>)
    ^step:
      %old_q = simulation.ref.load %q : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %old_r = simulation.ref.load %r : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %one = simulation.logic.constant 1 : i8, 0 : i8 : !simulation.logic<8>
      %next_q = simulation.logic.binary add %old_q, %one : !simulation.logic<8>
      %next_r = simulation.logic.binary add %old_r, %one : !simulation.logic<8>
      cf.br ^publish(%next_q, %next_r : !simulation.logic<8>, !simulation.logic<8>)
    ^publish(%q_value: !simulation.logic<8>, %r_value: !simulation.logic<8>):
      simulation.nba.enqueue %q_value to %q : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      simulation.nba.enqueue %r_value to %r : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
  }
}

// CHECK-DAG: llvm.mlir.global internal @__obelisk_eval_selected_variant_v1_0
// CHECK-DAG: llvm.func @lane.__obelisk_eval_body_0.__obelisk_path_known
// CHECK-LABEL: llvm.func {{.*}}@__obelisk_eval_path_dispatch_v1_0(
// CHECK: llvm.mlir.addressof @__obelisk_eval_selected_variant_v1_0
// CHECK: llvm.load
// CHECK: llvm.cond_br
// CHECK: llvm.call @lane.__obelisk_eval_body_0.__obelisk_two_state
// CHECK: llvm.call @lane.__obelisk_eval_body_0(
// CHECK: llvm.call @lane.__obelisk_eval_body_0.__obelisk_path_known
// PROBE: llvm.func @lane.__obelisk_eval_body_0.__obelisk_path_known
// Writable VPI can force this small fixture onto the ordered NBA fallback.
// WRITE-NOT: llvm.func @lane.__obelisk_eval_body_0.__obelisk_two_state
// WRITE: llvm.func @lane.__obelisk_eval_body_0(
// WRITE-NOT: llvm.func @lane.__obelisk_eval_body_0.__obelisk_two_state
