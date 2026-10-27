// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(talos-sim-thread-process-cfg),talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),encode-talos-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=DIRECT
// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph),encode-talos-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-talos-sim-processes-to-llvm-coroutines)' | FileCheck %s --check-prefix=RUNTIME

// The entry check of a gated activation copy (schedule.known.roots) tests
// one bit of the generated unknown summary per root, one summary word at a
// time; roots are numbered in the order gates first test them, so this
// check's three roots share word 0 (mask 7). A set bit sends the check to the
// runtime, which rescans the gate's dirty roots before answering, unless the
// gate's backoff state still counts failed tests to answer without calling.
// The generated main binds the summary, its root records and patterns.
// Every generated store into the unknown plane of a summarized root sets the
// root's bit when it writes X/Z, out of line: here the original body's store
// of `out` (bit 2 of word 0, at plane byte 14). The copy's store of `out` is
// known and writes only the value plane. Without a static state layout,
// stores go through the runtime, which reports X/Z writes itself, and the
// copy again stores only the value plane.

// DIRECT: llvm.mlir.global internal @__talos_state_unknown_summary_v1(dense<7> : tensor<1xi64>)
// DIRECT-NEXT: llvm.mlir.global internal constant @__talos_state_unknown_summary_roots_v1(dense<[0, 8, 0, 8, 100, 0, 112, 8, 0]> : tensor<9xi64>)
// DIRECT-NEXT: llvm.mlir.global internal constant @__talos_state_unknown_summary_patterns_v1(dense<0> : tensor<2xi64>)
// DIRECT-NEXT: llvm.mlir.global internal @__talos_state_unknown_summary_backoff_v1(dense<0> : tensor<1xi64>)
// DIRECT-LABEL: llvm.func @step(
// DIRECT-DAG: %[[ZERO:.*]] = llvm.mlir.constant(0 : i64) : i64
// DIRECT-DAG: %[[MASK:.*]] = llvm.mlir.constant(7 : i64) : i64
// DIRECT-DAG: %[[OUTBIT:.*]] = llvm.mlir.constant(4 : i64) : i64
// DIRECT-DAG: %[[PLANE:.*]] = llvm.mlir.addressof @__talos_state_unknown :
// DIRECT-DAG: %[[SUMMARY:.*]] = llvm.mlir.addressof @__talos_state_unknown_summary_v1
// DIRECT-DAG: %[[BACKOFF:.*]] = llvm.mlir.addressof @__talos_state_unknown_summary_backoff_v1
// DIRECT-DAG: %[[PAIRS:.*]] = llvm.alloca %{{.*}} x i64
// DIRECT: %[[WORD:.*]] = llvm.load %[[SUMMARY]] {alignment = 8 : i64} : !llvm.ptr -> i64
// DIRECT-NEXT: %[[DIRTY:.*]] = llvm.and %[[WORD]], %[[MASK]]
// DIRECT-NEXT: %[[CLEAN:.*]] = llvm.icmp "eq" %[[DIRTY]], %[[ZERO]]
// DIRECT-NEXT: llvm.cond_br %[[CLEAN]] weights([1048575, 1]), ^[[DECIDE:bb[0-9]+]](%{{.*}} : i1), ^[[REFRESH:bb[0-9]+]]
// DIRECT: ^[[REFRESH]]:
// DIRECT-NEXT: %[[STATE:.*]] = llvm.load %[[BACKOFF]] {alignment = 8 : i64} : !llvm.ptr -> i64
// DIRECT-NEXT: %[[WAITS:.*]] = llvm.trunc %[[STATE]] : i64 to i32
// DIRECT-NEXT: %[[WAITING:.*]] = llvm.icmp "ne" %[[WAITS]], %{{.*}} : i32
// DIRECT-NEXT: llvm.cond_br %[[WAITING]] weights([1048575, 1]), ^[[WAIT:bb[0-9]+]], ^[[CALL:bb[0-9]+]]
// DIRECT: ^[[WAIT]]:
// DIRECT-NEXT: %[[LESS:.*]] = llvm.sub %[[STATE]], %{{.*}} : i64
// DIRECT-NEXT: llvm.store %[[LESS]], %[[BACKOFF]]
// DIRECT-NEXT: llvm.br ^[[DECIDE]](%{{.*}} : i1)
// DIRECT: ^[[CALL]]:
// DIRECT-NEXT: llvm.store %[[ZERO]], %[[PAIRS]]
// DIRECT: llvm.store %[[MASK]], %{{.*}}
// DIRECT: %[[R:.*]] = llvm.call @talos_rt_v1_native_state_refresh_unknown_summary(%{{.*}}, %[[PAIRS]], %{{.*}}, %[[BACKOFF]])
// DIRECT-NEXT: %[[OK:.*]] = llvm.icmp "ne" %[[R]], %{{.*}} : i32
// DIRECT-NEXT: llvm.br ^[[DECIDE]](%[[OK]] : i1)
// DIRECT: ^[[DECIDE]](%[[KNOWN:.*]]: i1):
// DIRECT-NEXT: llvm.cond_br %[[KNOWN]] weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
// DIRECT: %[[OUT:.*]] = llvm.getelementptr %[[PLANE]][14]
// DIRECT-NEXT: llvm.store %[[X:.*]], %[[OUT]] {alignment = 1 : i64} : i8, !llvm.ptr
// DIRECT-NEXT: %[[HASX:.*]] = llvm.icmp "ne" %[[X]], %{{.*}} : i8
// DIRECT-NEXT: llvm.cond_br %[[HASX]] weights([1, 1048575]), ^[[MARK:bb[0-9]+]], ^
// DIRECT: ^[[MARK]]:
// DIRECT-NEXT: %[[OLD:.*]] = llvm.load %[[SUMMARY]]
// DIRECT-NEXT: %[[NEW:.*]] = llvm.or %[[OLD]], %[[OUTBIT]]
// DIRECT-NEXT: llvm.store %[[NEW]], %[[SUMMARY]]
// DIRECT: ^[[COPY]]:
// DIRECT-NOT: %[[PLANE]]
// DIRECT: llvm.return
// DIRECT: llvm.func @main(
// DIRECT: llvm.call @talos_rt_v1_native_state_bind_unknown_summary(

// RUNTIME-LABEL: llvm.func @step(
// RUNTIME-DAG: %[[PLANE:.*]] = llvm.mlir.addressof @__talos_state_unknown :
// RUNTIME-DAG: %[[SUMMARY:.*]] = llvm.mlir.addressof @__talos_state_unknown_summary_v1
// RUNTIME: llvm.load %[[SUMMARY]]
// RUNTIME: llvm.call @talos_rt_v1_native_state_refresh_unknown_summary(
// RUNTIME: llvm.cond_br %{{.*}} weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
// RUNTIME: llvm.call @talos_rt_v1_native_state_store_plane(%{{.*}}, %[[PLANE]],
// RUNTIME: ^[[COPY]]:
// RUNTIME-NOT: llvm.call @talos_rt_v1_native_state_store_plane(%{{.*}}, %[[PLANE]],
// RUNTIME: llvm.return

!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
!l10 = !simulation.logic<10>
!l100 = !simulation.logic<100>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_roots {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.step"
    simulation.storage.decl 0 in 0 : !l8 design hierarchy "top.a"
    simulation.storage.decl 1 in 0 : !l100 design hierarchy "top.wide"
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

    // p = a[5:2] * wide[13:4], q = wide[67:60] * wide[99:96], s = p + q,
    // out = s * p * q * s * (s * p): the check covers a, wide and out.
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
