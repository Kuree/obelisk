// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s

// A store at a run-time index into a summarized root reports the root
// through the range its lowering records for the write: the original body's
// store of `mem[i]` sets the memory's summary bit when the stored unknown
// byte is nonzero. The gated copy reads `mem` by value only, but its
// quotient may still be X (division by zero, IEEE 1800-2023 11.4.2), so it
// stores both planes and reports the same way.

// CHECK-LABEL: llvm.func @step(
// CHECK-DAG: %[[UNKNOWN:.*]] = llvm.mlir.addressof @__talos_state_unknown :
// CHECK-DAG: %[[SUMMARY:.*]] = llvm.mlir.addressof @__talos_state_unknown_summary_v1 :
// CHECK: llvm.getelementptr %[[UNKNOWN]][%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
// CHECK: %[[ADDRESS:.*]] = llvm.getelementptr %[[UNKNOWN]][%{{.*}}] : (!llvm.ptr, i64) -> !llvm.ptr, i8
// CHECK: llvm.store %[[X:.*]], %[[ADDRESS]]
// CHECK-NEXT: %[[HASX:.*]] = llvm.icmp "ne" %[[X]], %{{.*}} : i8
// CHECK-NEXT: llvm.cond_br %[[HASX]] weights([1, 1048575]), ^[[REPORT:bb[0-9]+]], ^
// CHECK: ^[[REPORT]]:
// CHECK-NEXT: %[[OLD:.*]] = llvm.load %[[SUMMARY]]
// CHECK-NEXT: llvm.or %[[OLD]], %{{.*}} : i64

!l3 = !simulation.logic<3>
!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!r3 = !simulation.ref<!l3>
!r8 = !simulation.ref<!l8>
!memory = !simulation.unpacked_array<0 : 7 x !l8>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_roots_dynamic {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.step"
    simulation.storage.decl 0 in 0 : !l3 design hierarchy "top.i"
    simulation.storage.decl 1 in 0 : !memory design hierarchy "top.mem"
    simulation.storage.decl 2 in 0 : !l8 design hierarchy "top.out"

    simulation.func @__talos_root(
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

    simulation.func private @step(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %i = simulation.context.storage %ctx[0] : !r3
      %mem = simulation.context.storage %ctx[1] : !simulation.ref<!memory>
      %out = simulation.context.storage %ctx[2] : !r8
      %short = simulation.ref.load %i : !r3 -> !l3
      %index = simulation.logic.resize %short signed = false : !l3 -> !l4
      %slot = simulation.ref.array_element %mem[%index] :
          (!simulation.ref<!memory>, !l4) -> !r8
      %e = simulation.ref.load %slot : !r8 -> !l8
      %p1 = simulation.logic.binary mul %e, %e : !l8
      %p2 = simulation.logic.binary add %p1, %e : !l8
      %p3 = simulation.logic.binary mul %p2, %p1 : !l8
      %r = simulation.logic.binary udiv %p3, %e : !l8
      simulation.ref.store %r to %slot : !l8, !r8
      simulation.ref.store %p2 to %out : !l8, !r8
      simulation.return
    }
  }
}
