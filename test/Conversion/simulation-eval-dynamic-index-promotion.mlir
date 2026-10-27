// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --implicit-check-not=__talos_eval_selected_variant_v1_ < %t.mlir

// A write through an invalid index performs no operation (IEEE 1800-2023
// 7.4.6), so a dynamic store needs no bounds proof to preserve known state:
// its guard is the hull of the array. The read uses a two-bit index into four
// elements, which is valid whenever the index is known. Both keep the lane's
// two-state variant.
!mem = !simulation.unpacked_array<0 : 3 x !simulation.logic<4>>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32,
  native.closed_executable
} {
  simulation.design @dynamic_index_promotion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "dynamic.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "dynamic.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "dynamic.lane"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !mem design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<2> design
    simulation.storage.decl 4 in 0 : !simulation.logic<4> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %levels = arith.constant 0 : i64
      %scope = simulation.bytes.constant ""
      simulation.dump.vars %ctx, %levels, %scope : (!simulation.context, i64, !simulation.bytes) -> ()
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      // Known initial state: bits nothing writes would hold X forever.
      %mem = simulation.context.storage %ctx[1] : !simulation.ref<!mem>
      %write = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %read = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<2>>
      %nibble = simulation.logic.constant 0 : i4, 0 : i4 : !simulation.logic<4>
      %cells = simulation.aggregate.splat %nibble : (!simulation.logic<4>) -> !mem
      simulation.ref.store %cells to %mem : !mem, !simulation.ref<!mem>
      %wi = simulation.logic.constant 7 : i8, 0 : i8 : !simulation.logic<8>
      simulation.ref.store %wi to %write : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      %ri = simulation.logic.constant 1 : i2, 0 : i2 : !simulation.logic<2>
      simulation.ref.store %ri to %read : !simulation.logic<2>, !simulation.ref<!simulation.logic<2>>
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
      %mem = simulation.context.storage %ctx[1] : !simulation.ref<!mem>
      %write = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %read = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<2>>
      %output = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<4>>
      %wi = simulation.ref.load %write : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %slot = simulation.ref.array_element %mem[%wi] : (!simulation.ref<!mem>, !simulation.logic<8>) -> !simulation.ref<!simulation.logic<4>>
      %five = simulation.logic.constant 5 : i4, 0 : i4 : !simulation.logic<4>
      simulation.ref.store %five to %slot : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      %ri = simulation.ref.load %read : !simulation.ref<!simulation.logic<2>> -> !simulation.logic<2>
      %rx = simulation.logic.resize %ri signed = false : !simulation.logic<2> -> !simulation.logic<3>
      %cell = simulation.ref.array_element %mem[%rx] : (!simulation.ref<!mem>, !simulation.logic<3>) -> !simulation.ref<!simulation.logic<4>>
      %value = simulation.ref.load %cell : !simulation.ref<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.ref.store %value to %output : !simulation.logic<4>, !simulation.ref<!simulation.logic<4>>
      cf.br ^wait
    }
  }
}
// CHECK-DAG: llvm.func @lane.__talos_eval_body_0.__talos_two_state_0
// CHECK-DAG: llvm.mlir.global internal @__talos_eval_kernel_promotion_latched_v1
// CHECK-DAG: llvm.func @lane.__talos_eval_body_0(%arg0: !llvm.ptr) attributes {{{.*}}schedule.eval.conditionally_two_state
// The whole memory [16,32) guards both accesses; the indices and the output
// follow it.
// CHECK-DAG: schedule.eval.local_promotion_ranges = array<i64: 16, 16, 32, 8, 40, 2, 48, 4>
