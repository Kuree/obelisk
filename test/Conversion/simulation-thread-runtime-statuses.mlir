// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-thread-runtime-statuses)' | FileCheck %s

module {
  obelisk_sim.design @thread_runtime_statuses {
    obelisk_sim.scope.decl 0
    obelisk_sim.class.decl @Box id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "status.leaf"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "status.caller"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "status.infallible"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "status.real_leaf"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "status.method"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "status.method_caller"

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

    obelisk_sim.func private @real_leaf(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %status_bits: i32 {obelisk_sim.capture_kind = 2 : i32},
        %value: f64 {obelisk_sim.capture_kind = 2 : i32}) -> f64
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %status = obelisk_rt.status.from_bits %status_bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return %value : f64
    }

    obelisk_sim.func private @method(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Box> {obelisk_sim.capture_kind = 1 : i32},
        %bits: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return %bits : i32
    }

    obelisk_sim.func private @method_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@Box> {obelisk_sim.capture_kind = 1 : i32},
        %bits: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %result = obelisk_sim.class.direct_call @method %this(%bits) :
          (!obelisk_sim.class_handle<@Box>, i32) -> i32
      obelisk_sim.return %result : i32
    }
  }

  // Identical local symbol names in independent designs must not share the
  // may-fail call graph.
  obelisk_sim.design @failing_scope {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 10 in 0 function hierarchy "failing.leaf"
    obelisk_sim.code_unit.decl 11 in 0 function hierarchy "failing.caller"
    obelisk_sim.func private @scoped_leaf(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %bits: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %status = obelisk_rt.status.from_bits %bits :
          (i32) -> !obelisk_rt.status
      obelisk_sim.status.check %status
      obelisk_sim.return %bits : i32
    }
    obelisk_sim.func private @scoped_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %bits: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %result = obelisk_sim.call @scoped_leaf(%ctx, %bits) :
          (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }
  }

  obelisk_sim.design @infallible_scope {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 20 in 0 function hierarchy "clean.leaf"
    obelisk_sim.code_unit.decl 21 in 0 function hierarchy "clean.caller"
    obelisk_sim.func private @scoped_leaf(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 20 : i64} {
      obelisk_sim.return %value : i32
    }
    obelisk_sim.func private @scoped_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64} {
      %result = obelisk_sim.call @scoped_leaf(%ctx, %value) :
          (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
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

// CHECK-LABEL: obelisk_sim.func private @real_leaf(
// CHECK-SAME: -> (f64, i32)
// CHECK: %[[REAL_OK:.*]] = obelisk_rt.status.is %{{.*}}, 0
// CHECK: cf.cond_br %[[REAL_OK]],
// CHECK: arith.constant 0.000000e+00 : f64
// CHECK: obelisk_sim.return %{{.*}}, %{{.*}} : f64, i32

// CHECK-LABEL: obelisk_sim.func private @method(
// CHECK-SAME: -> (i32, i32)
// CHECK-LABEL: obelisk_sim.func private @method_caller(
// CHECK-SAME: -> (i32, i32)
// CHECK: %[[METHOD_RESULTS:.*]]:2 = obelisk_sim.class.direct_call @method
// CHECK: obelisk_rt.status.from_bits %[[METHOD_RESULTS]]#1

// CHECK-LABEL: obelisk_sim.design @failing_scope
// CHECK-LABEL: obelisk_sim.func private @scoped_leaf(
// CHECK-SAME: -> (i32, i32)
// CHECK-LABEL: obelisk_sim.func private @scoped_caller(
// CHECK-SAME: -> (i32, i32)

// CHECK-LABEL: obelisk_sim.design @infallible_scope
// CHECK-LABEL: obelisk_sim.func private @scoped_leaf(
// CHECK-SAME: -> i32
// CHECK-LABEL: obelisk_sim.func private @scoped_caller(
// CHECK-SAME: -> i32
