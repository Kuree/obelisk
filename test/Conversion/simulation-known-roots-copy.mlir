// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// A copy into the unknown plane of a summarized root reports the root when
// what it copies differs from the root's steady pattern
// (`simulation.steady_unknown`, here bits [44, 48) of `dst`): before the
// copy, each source word is compared with the pattern bits it lands on.

// CHECK-LABEL: llvm.func @step(
// CHECK-DAG: %[[PATTERN:.*]] = llvm.mlir.constant(263882790666240 : i64) : i64
// CHECK-DAG: %[[SUMMARY:.*]] = llvm.mlir.addressof @__obelisk_state_unknown_summary_v1 :
// CHECK: %[[FIRST:.*]] = llvm.load %[[SOURCE:.*]] {alignment = 1 : i64} : !llvm.ptr -> i64
// CHECK-NEXT: %[[DIFF:.*]] = llvm.xor %[[FIRST]], %[[PATTERN]] : i64
// CHECK: %[[CHANGED:.*]] = llvm.icmp "ne" %{{.*}}, %{{.*}} : i64
// CHECK-NEXT: "llvm.intr.memcpy"(%{{.*}}, %[[SOURCE]], %{{.*}})
// CHECK-NEXT: llvm.cond_br %[[CHANGED]] weights([1, 1048575]), ^[[REPORT:bb[0-9]+]], ^
// CHECK: ^[[REPORT]]:
// CHECK-NEXT: llvm.load %[[SUMMARY]]

!l8 = !simulation.logic<8>
!l32 = !simulation.logic<32>
!a = !simulation.unpacked_array<0 : 7 x !l32>
!ra = !simulation.ref<!a>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_roots_copy {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.step"
    simulation.storage.decl 0 in 0 : !a design hierarchy "top.src"
    simulation.storage.decl 1 in 0 : !a design hierarchy "top.dst" {simulation.steady_unknown = array<i64: 44, 4>}
    simulation.storage.decl 2 in 0 : !l8 design hierarchy "top.out"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %process = simulation.spawn @initial(%ctx) :
          !simulation.context -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      simulation.call @step(%ctx) : (!simulation.context) -> ()
      simulation.return
    }

    // out = (t * t * t + t) * t * t * t for t = dst[2][7:0]; dst = src.
    simulation.func private @step(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %src = simulation.context.storage %ctx[0] : !ra
      %dst = simulation.context.storage %ctx[1] : !ra
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!l8>
      %two = simulation.ref.subelement %dst[[2]] : !ra -> !simulation.ref<!l32>
      %d = simulation.ref.load %two : !simulation.ref<!l32> -> !l32
      %t = simulation.logic.resize %d signed = false : !l32 -> !l8
      %p = simulation.logic.binary mul %t, %t : !l8
      %q = simulation.logic.binary mul %p, %t : !l8
      %r = simulation.logic.binary add %q, %t : !l8
      %u = simulation.logic.binary mul %r, %q : !l8
      simulation.ref.store %u to %out : !l8, !simulation.ref<!l8>
      %whole = simulation.ref.load %src : !ra -> !a
      simulation.ref.store %whole to %dst : !a, !ra
      simulation.return
    }
  }
}
