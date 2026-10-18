// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},schedule-version-known-activations{minimum-benefit=1},convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// A root's steady pattern (`simulation.steady_unknown`, here a[7:4]) is
// part of its summary record: a clear bit means its unknown plane equals the
// pattern, so the copy's value-only read of `a` loads those bits as a
// constant X. The low nibble stays known, so the arithmetic on it is
// two-state and its store is value-only, while the copy of `a` stores the
// constant X nibble with its value into a root no gate tests. Storing `a`
// back keeps both planes: the copy stores the pattern itself, which needs no
// report, and the original body reports when what it stores differs from
// the pattern.

// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_state_unknown_summary_roots_v1(dense<[0, 8, 4294967296, 8, 8, 1]> : tensor<6xi64>)
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_state_unknown_summary_patterns_v1(dense<4> : tensor<2xi64>)
// CHECK-LABEL: llvm.func @step(
// CHECK-DAG: %[[NIBBLE:.*]] = llvm.mlir.constant(-16 : i8) : i8
// CHECK-DAG: %[[UNKNOWN:.*]] = llvm.mlir.addressof @__obelisk_state_unknown :
// CHECK-DAG: %[[VALUE:.*]] = llvm.mlir.addressof @__obelisk_state_value :
// CHECK-DAG: %[[SUMMARY:.*]] = llvm.mlir.addressof @__obelisk_state_unknown_summary_v1 :
// CHECK: llvm.call @obelisk_rt_v1_native_state_refresh_unknown_summary(
// CHECK: llvm.cond_br %{{.*}} weights([1048575, 1]), ^[[COPY:bb[0-9]+]], ^
// CHECK: llvm.store %[[AX:.*]], %[[UNKNOWN]] {alignment = 1 : i64} : i8, !llvm.ptr
// CHECK-NEXT: %[[DIFF:.*]] = llvm.xor %[[AX]], %[[NIBBLE]] : i8
// CHECK-NEXT: %[[CHANGED:.*]] = llvm.icmp "ne" %[[DIFF]]
// CHECK-NEXT: llvm.cond_br %[[CHANGED]] weights([1, 1048575]), ^[[REPORT:bb[0-9]+]], ^
// CHECK: ^[[REPORT]]:
// CHECK-NEXT: llvm.load %[[SUMMARY]]
// CHECK: ^[[COPY]]:
// CHECK-NOT: %[[UNKNOWN]]
// CHECK: %[[COPYX:.*]] = llvm.getelementptr %[[UNKNOWN]][2]
// CHECK-NEXT: llvm.store %[[NIBBLE]], %[[COPYX]] {alignment = 1 : i64} : i8, !llvm.ptr
// CHECK-NEXT: llvm.store %{{.*}}, %[[VALUE]] {alignment = 1 : i64} : i8, !llvm.ptr
// CHECK-NEXT: llvm.store %[[NIBBLE]], %[[UNKNOWN]] {alignment = 1 : i64} : i8, !llvm.ptr
// CHECK-NEXT: llvm.return

!l4 = !simulation.logic<4>
!l8 = !simulation.logic<8>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  native.optimization_level = 3 : i32
} {
  simulation.design @known_roots_steady {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.initial"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.step"
    simulation.storage.decl 0 in 0 : !l8 design hierarchy "top.a" {simulation.steady_unknown = array<i64: 4, 4>}
    simulation.storage.decl 1 in 0 : !l8 design hierarchy "top.out"
    simulation.storage.decl 2 in 0 : !l8 design hierarchy "top.copy"

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

    // out = lo * lo * lo + lo for lo = a[3:0]; copy = a.
    simulation.func private @step(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %a = simulation.context.storage %ctx[0] : !simulation.ref<!l8>
      %out = simulation.context.storage %ctx[1] : !simulation.ref<!l8>
      %copy = simulation.context.storage %ctx[2] : !simulation.ref<!l8>
      %ra = simulation.ref.extract %a from 0 : !simulation.ref<!l8> -> !simulation.ref<!l4>
      %lo = simulation.ref.load %ra : !simulation.ref<!l4> -> !l4
      %x = simulation.logic.resize %lo signed = false : !l4 -> !l8
      %p = simulation.logic.binary mul %x, %x : !l8
      %q = simulation.logic.binary mul %p, %x : !l8
      %r = simulation.logic.binary add %q, %x : !l8
      %s = simulation.logic.binary mul %r, %q : !l8
      simulation.ref.store %s to %out : !l8, !simulation.ref<!l8>
      %whole = simulation.ref.load %a : !simulation.ref<!l8> -> !l8
      simulation.ref.store %whole to %copy : !l8, !simulation.ref<!l8>
      simulation.ref.store %whole to %a : !l8, !simulation.ref<!l8>
      simulation.return
    }
  }
}
