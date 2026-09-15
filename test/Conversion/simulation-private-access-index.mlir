// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s
// A shared index must distinguish roots and accessor identities. Two private
// roots can both promote during the same sweep. A second accessor blocks one
// other root; passing an otherwise private root to a different spawn target
// blocks another, even when that target does not currently load the reference.
module attributes {obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @access_index {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "private_first"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "private_second"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "shared"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "escaped"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "other_reader"
    obelisk_sim.code_unit.decl 7 in 0 initial hierarchy "other_target"
    obelisk_sim.storage.decl 0 in 0 : i32 static
    obelisk_sim.storage.decl 1 in 0 : i32 static
    obelisk_sim.storage.decl 2 in 0 : i32 static
    obelisk_sim.storage.decl 3 in 0 : i32 static
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %r0 = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i32>
      %p0 = obelisk_sim.spawn @private_first(%ctx, %r0) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      %r1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i32>
      %p1 = obelisk_sim.spawn @private_second(%ctx, %r1) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      %r2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i32>
      %p2 = obelisk_sim.spawn @shared(%ctx, %r2) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      %r3 = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<i32>
      %p3 = obelisk_sim.spawn @escaped(%ctx, %r3) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      %reader = obelisk_sim.spawn @other_reader(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %target = obelisk_sim.spawn @other_target(%ctx, %r3) : !obelisk_sim.context, !obelisk_sim.ref<i32> -> !obelisk_sim.process
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @private_first(
    // CHECK-NOT: obelisk_sim.ref.
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @private_first(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %value = arith.constant 1 : i32
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @private_second(
    // CHECK-NOT: obelisk_sim.ref.
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @private_second(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %value = arith.constant 2 : i32
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @shared(
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @shared(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %value = arith.constant 3 : i32
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @escaped(
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.ref.load
    // CHECK: obelisk_sim.return
    obelisk_sim.func private @escaped(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %value = arith.constant 4 : i32
      obelisk_sim.ref.store %value to %ref : i32, !obelisk_sim.ref<i32>
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    // CHECK-LABEL: obelisk_sim.func private @other_reader(
    // CHECK: obelisk_sim.ref.load
    obelisk_sim.func private @other_reader(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %ref = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<i32>
      %loaded = obelisk_sim.ref.load %ref : !obelisk_sim.ref<i32> -> i32
      obelisk_sim.return
    }
    obelisk_sim.func private @other_target(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<i32> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 7 : i64} {
      obelisk_sim.return
    }
  }
}
