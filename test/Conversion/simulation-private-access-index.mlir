// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// A shared index must distinguish roots and accessor identities. Two private
// roots can both promote during the same sweep. A second accessor blocks one
// other root; passing an otherwise private root to a different spawn target
// blocks another, even when that target does not currently load the reference.
module attributes {schedule.native_scheduler = 3 : i32} {
  simulation.design @access_index {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "private_first"
    simulation.code_unit.decl 3 in 0 initial hierarchy "private_second"
    simulation.code_unit.decl 4 in 0 initial hierarchy "shared"
    simulation.code_unit.decl 5 in 0 initial hierarchy "escaped"
    simulation.code_unit.decl 6 in 0 initial hierarchy "other_reader"
    simulation.code_unit.decl 7 in 0 initial hierarchy "other_target"
    simulation.storage.decl 0 in 0 : i32 static
    simulation.storage.decl 1 in 0 : i32 static
    simulation.storage.decl 2 in 0 : i32 static
    simulation.storage.decl 3 in 0 : i32 static
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %r0 = simulation.context.storage %ctx[0] : !simulation.ref<i32>
      %p0 = simulation.spawn @private_first(%ctx, %r0) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<i32>
      %p1 = simulation.spawn @private_second(%ctx, %r1) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %p2 = simulation.spawn @shared(%ctx, %r2) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<i32>
      %p3 = simulation.spawn @escaped(%ctx, %r3) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      %reader = simulation.spawn @other_reader(%ctx) : !simulation.context -> !simulation.process
      %target = simulation.spawn @other_target(%ctx, %r3) : !simulation.context, !simulation.ref<i32> -> !simulation.process
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @private_first(
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return
    simulation.func private @private_first(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 1 : i32
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      %loaded = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @private_second(
    // CHECK-NOT: simulation.ref.
    // CHECK: simulation.return
    simulation.func private @private_second(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %value = arith.constant 2 : i32
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      %loaded = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @shared(
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    simulation.func private @shared(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %value = arith.constant 3 : i32
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      %loaded = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @escaped(
    // CHECK: simulation.ref.store
    // CHECK: simulation.ref.load
    // CHECK: simulation.return
    simulation.func private @escaped(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %value = arith.constant 4 : i32
      simulation.ref.store %value to %ref : i32, !simulation.ref<i32>
      %loaded = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @other_reader(
    // CHECK: simulation.ref.load
    simulation.func private @other_reader(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %ref = simulation.context.storage %ctx[2] : !simulation.ref<i32>
      %loaded = simulation.ref.load %ref : !simulation.ref<i32> -> i32
      simulation.return
    }
    simulation.func private @other_target(%ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<i32> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      simulation.return
    }
  }
}
