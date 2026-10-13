// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s < %t.mlir

// Only off-diagonal matrix elements are reachable. The whole-array guard
// must exclude the uninitialized diagonal while retaining the output root, so
// an X/Z store to the diagonal does not unpromote the kernel. Stores publish
// X/Z through __obelisk_eval_promotion_publish_unknown_v1
// (simulation-eval-promotion-word-scan.mlir), and the runtime invalidates only
// proofs whose ranges overlap (runtime/unittests/PromotionRangeIndexTest.cpp).
!row = !simulation.unpacked_array<0 : 2 x !simulation.logic<3>>
!matrix = !simulation.unpacked_array<0 : 2 x !row>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32,
  obelisk.native.closed_executable
} {
  simulation.design @access_promotion {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "reset.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "reset.clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "reset.lane"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !matrix design
    simulation.storage.decl 2 in 0 : !simulation.logic<3> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %levels = arith.constant 0 : i64
      %scope = simulation.bytes.constant ""
      simulation.dump.vars %ctx, %levels, %scope : (!simulation.context, i64, !simulation.bytes) -> ()
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
      %root = simulation.context.storage %ctx[1] : !simulation.ref<!matrix>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<3>>
      %zero = arith.constant 0 : i32
      %one = arith.constant 1 : i32
      %limit = arith.constant 3 : i32
      %clear = simulation.logic.constant 0 : i3, 0 : i3 : !simulation.logic<3>
      simulation.ref.store %clear to %output : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      cf.br ^outer(%zero : i32)
    ^outer(%i: i32):
      %more_i = arith.cmpi slt, %i, %limit : i32
      cf.cond_br %more_i, ^inner(%zero : i32), ^done
    ^inner(%j: i32):
      %more_j = arith.cmpi slt, %j, %limit : i32
      cf.cond_br %more_j, ^select, ^outer_next
    ^select:
      %different = arith.cmpi ne, %i, %j : i32
      cf.cond_br %different, ^access, ^inner_next
    ^access:
      %row = simulation.ref.array_element %root[%i] : (!simulation.ref<!matrix>, i32) -> !simulation.ref<!row>
      %element = simulation.ref.array_element %row[%j] : (!simulation.ref<!row>, i32) -> !simulation.ref<!simulation.logic<3>>
      %value = simulation.ref.load %element : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %old = simulation.ref.load %output : !simulation.ref<!simulation.logic<3>> -> !simulation.logic<3>
      %next = simulation.logic.binary xor %old, %value : !simulation.logic<3>
      simulation.ref.store %next to %output : !simulation.logic<3>, !simulation.ref<!simulation.logic<3>>
      cf.br ^inner_next
    ^inner_next:
      %next_j = arith.addi %j, %one : i32
      cf.br ^inner(%next_j : i32)
    ^outer_next:
      %next_i = arith.addi %i, %one : i32
      cf.br ^outer(%next_i : i32)
    ^done:
      cf.br ^wait
    }
  }
}
// CHECK-DAG: llvm.func @lane.__obelisk_eval_body_0.__obelisk_two_state_0
// CHECK-DAG: llvm.mlir.global internal @__obelisk_eval_selected_variant_v1_0
// Matrix is at bit 32. Its reached relative intervals are [3,12), [15,24).
// Output [64,67) remains required, including its unknown-plane store contract.
// CHECK-DAG: schedule.eval.local_promotion_ranges = array<i64: 35, 9, 47, 9, 64, 3>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_kernel_promotion_ranges_v1(dense<[0, 1, -34359738368, 17592186044415, 0, 1, -140737488355328, 72057594037927935, 1, 2, -1, 7]>
