// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=DIRECT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=RUNTIME

// The entry check of a gated activation copy (schedule.known.bits) reads
// only the unknown planes of the gated bits: one 64-bit word at a time with
// a mask of the selected bits, and a plane's final bytes one at a time.
// With a static state layout every root has a fixed direct location: the
// check reads the canonical plane in place and merges the words of all
// roots. Without one, the runtime loads each reference's current unknown
// plane, where an unresolved location reads as X, so the check never claims
// bits it cannot observe.

// DIRECT-LABEL: llvm.func @step(
// DIRECT-DAG: %[[MASK0:.*]] = llvm.mlir.constant(4190268 : i64) : i64
// DIRECT-DAG: %[[MASK1:.*]] = llvm.mlir.constant(16492674420720 : i64) : i64
// DIRECT-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
// DIRECT-DAG: %[[PLANE:.*]] = llvm.mlir.addressof @__obelisk_state_unknown
// DIRECT-NOT: obelisk_rt_v1_native_state_load_plane
// DIRECT: %[[WORD0:.*]] = llvm.load %[[PLANE]] {alignment = 1 : i64} : !llvm.ptr -> i64
// DIRECT-NEXT: llvm.and %[[WORD0]], %[[MASK0]]
// DIRECT: llvm.getelementptr %[[PLANE]][8] : (!llvm.ptr) -> !llvm.ptr, i8
// DIRECT-NEXT: llvm.load %{{.*}} : !llvm.ptr -> i8
// DIRECT: llvm.getelementptr %[[PLANE]][14] : (!llvm.ptr) -> !llvm.ptr, i8
// DIRECT: %[[TAIL:.*]] = llvm.and %{{.*}}, %[[MASK1]]
// DIRECT-NEXT: %[[ANY:.*]] = llvm.or %{{.*}}, %[[TAIL]]
// DIRECT-NEXT: %[[OK:.*]] = llvm.icmp "eq" %[[ANY]], %[[ZERO]]
// DIRECT-NEXT: llvm.cond_br %[[OK]] weights([1048575, 1])

// RUNTIME-LABEL: llvm.func @step(
// RUNTIME-DAG: %[[ONE:.*]] = llvm.mlir.constant(1 : i32) : i32
// RUNTIME-DAG: %[[SIX:.*]] = llvm.mlir.constant(6 : i64) : i64
// RUNTIME-DAG: %[[HUNDRED:.*]] = llvm.mlir.constant(100 : i64) : i64
// RUNTIME-DAG: %[[MASKA:.*]] = llvm.mlir.constant(60 : i64) : i64
// RUNTIME-DAG: %[[MASK0:.*]] = llvm.mlir.constant(-1152921504606830608 : i64) : i64
// RUNTIME-DAG: %[[MASK1:.*]] = llvm.mlir.constant(64424509455 : i64) : i64
// RUNTIME-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
// RUNTIME-DAG: %[[PLANE:.*]] = llvm.mlir.addressof @__obelisk_state_unknown
// RUNTIME: llvm.call @obelisk_rt_v1_native_state_load_plane(%{{.*}}, %[[PLANE]], %{{.*}}, %{{.*}}, %[[SIX]], %[[ONE]], %[[ONE]], %[[A:.*]]) :
// RUNTIME-NEXT: llvm.load %[[A]] {alignment = 1 : i64} : !llvm.ptr -> i8
// RUNTIME: llvm.and %{{.*}}, %[[MASKA]]
// RUNTIME: llvm.call @obelisk_rt_v1_native_state_load_plane(%{{.*}}, %[[PLANE]], %{{.*}}, %{{.*}}, %[[HUNDRED]], %[[ONE]], %[[ONE]], %[[WIDE:.*]]) :
// RUNTIME-NEXT: %[[WORD0:.*]] = llvm.load %[[WIDE]] {alignment = 1 : i64} : !llvm.ptr -> i64
// RUNTIME-NEXT: llvm.and %[[WORD0]], %[[MASK0]]
// RUNTIME: llvm.getelementptr %[[WIDE]][12] : (!llvm.ptr) -> !llvm.ptr, i8
// RUNTIME: %[[TAIL:.*]] = llvm.and %{{.*}}, %[[MASK1]]
// RUNTIME-NEXT: %[[ANY:.*]] = llvm.or %{{.*}}, %[[TAIL]]
// RUNTIME-NEXT: %[[OK:.*]] = llvm.icmp "eq" %[[ANY]], %[[ZERO]]
// RUNTIME-NEXT: llvm.cond_br %[[OK]] weights([1048575, 1])

!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!l10 = !simulation.logic<10>
!l100 = !simulation.logic<100>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_bits {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.step"
    simulation.storage.decl 0 in 0 : !l8 design hierarchy "top.a"
    simulation.storage.decl 1 in 0 : !l100 design hierarchy "top.wide"
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

    // p = a[5:2] * wide[13:4], q = wide[67:60] * wide[99:96], s = p + q,
    // out = s * p * q * s * (s * p): the check covers a[5:2] and three
    // slices of wide.
    simulation.func private @step(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!l8>
      %wide = simulation.context.storage %ctx[1] : !simulation.ref<!l100>
      %out = simulation.context.storage %ctx[2] : !simulation.ref<!l8>
      %ra = simulation.ref.extract %a from 2 : !simulation.ref<!l8> -> !simulation.ref<!l4>
      %r0 = simulation.ref.extract %wide from 4 : !simulation.ref<!l100> -> !simulation.ref<!l10>
      %r1 = simulation.ref.extract %wide from 60 : !simulation.ref<!l100> -> !simulation.ref<!l8>
      %r2 = simulation.ref.extract %wide from 96 : !simulation.ref<!l100> -> !simulation.ref<!l4>
      %va = simulation.ref.load %ra : !simulation.ref<!l4> -> !l4
      %v0 = simulation.ref.load %r0 : !simulation.ref<!l10> -> !l10
      %v1 = simulation.ref.load %r1 : !simulation.ref<!l8> -> !l8
      %v2 = simulation.ref.load %r2 : !simulation.ref<!l4> -> !l4
      %xa = simulation.logic.resize %va signed = false : !l4 -> !l8
      %x0 = simulation.logic.resize %v0 signed = false : !l10 -> !l8
      %x2 = simulation.logic.resize %v2 signed = false : !l4 -> !l8
      %p = simulation.logic.binary mul %xa, %x0 : !l8
      %q = simulation.logic.binary mul %v1, %x2 : !l8
      %s = simulation.logic.binary add %p, %q : !l8
      %t = simulation.logic.binary mul %s, %p : !l8
      %u = simulation.logic.binary mul %t, %q : !l8
      %v = simulation.logic.binary mul %u, %s : !l8
      %w = simulation.logic.binary mul %v, %t : !l8
      simulation.ref.store %w to %out : !l8, !simulation.ref<!l8>
      simulation.return
    }
  }
}
