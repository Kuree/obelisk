// RUN: obelisk-opt %s --split-input-file --allow-unregistered-dialect --pass-pipeline='builtin.module(simulation.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s

module {
  simulation.design @pinned_results {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.public_result"
    simulation.code_unit.decl 2 in 0 function hierarchy "top.nested_result"
    simulation.code_unit.decl 3 in 0 function hierarchy "top.address_result"
    simulation.code_unit.decl 4 in 0 function hierarchy "top.metadata_result"
    simulation.storage.decl 0 in 0 : i8 design

    // These calls remain active because each function writes storage. Their
    // unused results must nevertheless remain because every ABI is pinned.
    // CHECK-LABEL: simulation.func @public_result(
    // CHECK-SAME: -> (i8 {test.result = "public"})
    simulation.func @public_result(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (i8 {test.result = "public"})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 1 : i8
      simulation.ref.store %value to %storage : i8, !simulation.ref<i8>
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func nested @nested_result(
    // CHECK-SAME: -> (i8 {test.result = "nested"})
    simulation.func nested @nested_result(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (i8 {test.result = "nested"})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 2 : i8
      simulation.ref.store %value to %storage : i8, !simulation.ref<i8>
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func private @address_result(
    // CHECK-SAME: -> (i8 {test.result = "address"})
    simulation.func private @address_result(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (i8 {test.result = "address"})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 3 : i8
      simulation.ref.store %value to %storage : i8, !simulation.ref<i8>
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func private @metadata_result(
    // CHECK-SAME: -> (i8 {test.result = "metadata"})
    simulation.func private @metadata_result(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (i8 {test.result = "metadata"})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64,
                    simulation.future_boundary = true} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i8>
      %value = arith.constant 4 : i8
      simulation.ref.store %value to %storage : i8, !simulation.ref<i8>
      simulation.return %value : i8
    }

    // CHECK-LABEL: simulation.func @root(
    // CHECK: %{{.*}} = simulation.call @public_result(%arg0) {res_attrs = [{test.call_result = "public"}]} : (!simulation.context) -> i8
    // CHECK: %{{.*}} = simulation.call @nested_result(%arg0) {res_attrs = [{test.call_result = "nested"}]} : (!simulation.context) -> i8
    // CHECK: %{{.*}} = simulation.call @address_result(%arg0) {res_attrs = [{test.call_result = "address"}]} : (!simulation.context) -> i8
    // CHECK: %{{.*}} = simulation.call @metadata_result(%arg0) {res_attrs = [{test.call_result = "metadata"}]} : (!simulation.context) -> i8
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %address = "arith.constant"()
          {test.address = @address_result, value = 0 : i8} : () -> i8
      %public = simulation.call @public_result(%ctx)
          {res_attrs = [{test.call_result = "public"}]}
          : (!simulation.context) -> i8
      %nested = simulation.call @nested_result(%ctx)
          {res_attrs = [{test.call_result = "nested"}]}
          : (!simulation.context) -> i8
      %taken = simulation.call @address_result(%ctx)
          {res_attrs = [{test.call_result = "address"}]}
          : (!simulation.context) -> i8
      %metadata = simulation.call @metadata_result(%ctx)
          {res_attrs = [{test.call_result = "metadata"}]}
          : (!simulation.context) -> i8
      simulation.return
    }
  }
}

// -----

module {
  simulation.design @unresolved_uses {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 function hierarchy "top.target"
    simulation.storage.decl 0 in 0 : i16 design

    // An opaque region may introduce an unknown symbol scope, so symbol-use
    // discovery must pin this otherwise-private ABI, including its result.
    // CHECK-LABEL: simulation.design @unresolved_uses
    // CHECK-LABEL: simulation.func private @target(
    // CHECK-SAME: -> (i16 {test.result = "unresolved"})
    simulation.func private @target(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        -> (i16 {test.result = "unresolved"})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %storage = simulation.context.storage %ctx[0] : !simulation.ref<i16>
      %value = arith.constant 5 : i16
      simulation.ref.store %value to %storage : i16, !simulation.ref<i16>
      simulation.return %value : i16
    }

    // CHECK-LABEL: simulation.func @root(
    // CHECK: %{{.*}} = simulation.call @target(%arg0) {res_attrs = [{test.call_result = "unresolved"}]} : (!simulation.context) -> i16
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      "mystery.scope"() ({
      }) : () -> ()
      %result = simulation.call @target(%ctx)
          {res_attrs = [{test.call_result = "unresolved"}]}
          : (!simulation.context) -> i16
      simulation.return
    }
  }
}
