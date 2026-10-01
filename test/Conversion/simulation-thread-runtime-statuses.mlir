// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-thread-runtime-statuses)' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-thread-runtime-statuses)' -o %t.threaded
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-thread-runtime-statuses)' --mlir-disable-threading -o %t.serial
// RUN: diff %t.threaded %t.serial


module {
  simulation.design @thread_runtime_statuses {
    simulation.scope.decl 0
    simulation.class.decl @Box id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.code_unit.decl 1 in 0 function hierarchy "status.leaf"
    simulation.code_unit.decl 2 in 0 function hierarchy "status.caller"
    simulation.code_unit.decl 3 in 0 function hierarchy "status.infallible"
    simulation.code_unit.decl 4 in 0 function hierarchy "status.real_leaf"
    simulation.code_unit.decl 5 in 0 function hierarchy "status.method"
    simulation.code_unit.decl 6 in 0 function hierarchy "status.method_caller"

    simulation.func private @leaf(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %status = runtime.status.from_bits %value :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return %value : i32
    }

    simulation.func private @caller(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %result = simulation.call @leaf(%ctx, %value) :
          (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }

    simulation.func private @infallible(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      simulation.return %value : i32
    }

    simulation.func private @real_leaf(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %status_bits: i32 {simulation.capture_kind = 2 : i32},
        %value: f64 {simulation.capture_kind = 2 : i32}) -> f64
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %status = runtime.status.from_bits %status_bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return %value : f64
    }

    simulation.func private @method(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Box> {simulation.capture_kind = 1 : i32},
        %bits: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return %bits : i32
    }

    simulation.func private @method_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Box> {simulation.capture_kind = 1 : i32},
        %bits: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %result = simulation.class.direct_call @method %this(%bits) :
          (!simulation.class_handle<@Box>, i32) -> i32
      simulation.return %result : i32
    }
  }

  // Identical local symbol names in independent designs must not share the
  // may-fail call graph.
  simulation.design @failing_scope {
    simulation.scope.decl 0
    simulation.code_unit.decl 10 in 0 function hierarchy "failing.leaf"
    simulation.code_unit.decl 11 in 0 function hierarchy "failing.caller"
    simulation.func private @scoped_leaf(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %bits: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 10 : i64} {
      %status = runtime.status.from_bits %bits :
          (i32) -> !runtime.status
      simulation.status.check %status
      simulation.return %bits : i32
    }
    simulation.func private @scoped_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %bits: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 11 : i64} {
      %result = simulation.call @scoped_leaf(%ctx, %bits) :
          (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }
  }

  simulation.design @infallible_scope {
    simulation.scope.decl 0
    simulation.code_unit.decl 20 in 0 function hierarchy "clean.leaf"
    simulation.code_unit.decl 21 in 0 function hierarchy "clean.caller"
    simulation.func private @scoped_leaf(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 20 : i64} {
      simulation.return %value : i32
    }
    simulation.func private @scoped_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 2 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 21 : i64} {
      %result = simulation.call @scoped_leaf(%ctx, %value) :
          (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }
  }
}

// CHECK-LABEL: simulation.func private @leaf(
// CHECK-SAME: -> (i32, i32)
// CHECK: %[[LEAF_OK:.*]] = runtime.status.is %{{.*}}, <ok>
// CHECK: cf.cond_br %[[LEAF_OK]],
// CHECK: simulation.return %{{.*}}, %{{.*}} : i32, i32

// CHECK-LABEL: simulation.func private @caller(
// CHECK-SAME: -> (i32, i32)
// CHECK: %[[CALL_RESULTS:.*]]:2 = simulation.call @leaf
// CHECK: %[[CALL_STATUS:.*]] = runtime.status.from_bits
// CHECK-SAME: %[[CALL_RESULTS]]#1
// CHECK: %[[CALL_OK:.*]] = runtime.status.is %[[CALL_STATUS]], <ok>
// CHECK: cf.cond_br %[[CALL_OK]],
// CHECK: simulation.return %[[CALL_RESULTS]]#0, %{{.*}} :
// CHECK-SAME: i32, i32

// CHECK-LABEL: simulation.func private @infallible(
// CHECK-SAME: -> i32

// CHECK-LABEL: simulation.func private @real_leaf(
// CHECK-SAME: -> (f64, i32)
// CHECK: %[[REAL_OK:.*]] = runtime.status.is %{{.*}}, <ok>
// CHECK: cf.cond_br %[[REAL_OK]],
// CHECK: arith.constant 0.000000e+00 : f64
// CHECK: simulation.return %{{.*}}, %{{.*}} : f64, i32

// CHECK-LABEL: simulation.func private @method(
// CHECK-SAME: -> (i32, i32)
// CHECK-LABEL: simulation.func private @method_caller(
// CHECK-SAME: -> (i32, i32)
// CHECK: %[[METHOD_RESULTS:.*]]:2 = simulation.class.direct_call @method
// CHECK: runtime.status.from_bits %[[METHOD_RESULTS]]#1

// CHECK-LABEL: simulation.design @failing_scope
// CHECK-LABEL: simulation.func private @scoped_leaf(
// CHECK-SAME: -> (i32, i32)
// CHECK-LABEL: simulation.func private @scoped_caller(
// CHECK-SAME: -> (i32, i32)

// CHECK-LABEL: simulation.design @infallible_scope
// CHECK-LABEL: simulation.func private @scoped_leaf(
// CHECK-SAME: -> i32
// CHECK-LABEL: simulation.func private @scoped_caller(
// CHECK-SAME: -> i32
