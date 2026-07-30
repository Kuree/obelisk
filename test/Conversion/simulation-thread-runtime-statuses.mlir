// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-thread-runtime-statuses)' | FileCheck %s

module {
  obelisk_sim.design @thread_runtime_statuses {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "status.leaf"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "status.caller"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "status.infallible"

    obelisk_sim.func private @leaf(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %status = obelisk_rt.status.from_bits %value :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return %value : i32
    }

    obelisk_sim.func private @caller(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %result = obelisk_sim.call @leaf(%ctx, %value) :
          (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }

    obelisk_sim.func private @infallible(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      obelisk_sim.return %value : i32
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @leaf(
// CHECK-SAME: -> (i32, i32)
// CHECK: %[[LEAF_OK:.*]] = obelisk_rt.status.is %{{.*}}, 0
// CHECK: cf.cond_br %[[LEAF_OK]],
// CHECK: obelisk_sim.return %{{.*}}, %{{.*}} : i32, i32

// CHECK-LABEL: obelisk_sim.func private @caller(
// CHECK-SAME: -> (i32, i32)
// CHECK: %[[CALL_RESULTS:.*]]:2 = obelisk_sim.call @leaf
// CHECK: %[[CALL_STATUS:.*]] = obelisk_rt.status.from_bits
// CHECK-SAME: %[[CALL_RESULTS]]#1
// CHECK: %[[CALL_OK:.*]] = obelisk_rt.status.is %[[CALL_STATUS]], 0
// CHECK: cf.cond_br %[[CALL_OK]],
// CHECK: obelisk_sim.return %[[CALL_RESULTS]]#0, %{{.*}} :
// CHECK-SAME: i32, i32

// CHECK-LABEL: obelisk_sim.func private @infallible(
// CHECK-SAME: -> i32
