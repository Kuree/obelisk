// RUN: obelisk-opt %s --split-input-file --allow-unregistered-dialect --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-eliminate-dead-boundaries))' | FileCheck %s

module {
  obelisk_sim.design @pinned_results {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.public_result"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "top.nested_result"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "top.address_result"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "top.metadata_result"
    obelisk_sim.storage.decl 0 in 0 : i8 design

    // These calls remain active because each function writes storage. Their
    // unused results must nevertheless remain because every ABI is pinned.
    // CHECK-LABEL: obelisk_sim.func @public_result(
    // CHECK-SAME: -> (i8 {test.result = "public"})
    obelisk_sim.func @public_result(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> (i8 {test.result = "public"})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 1 : i8
      obelisk_sim.ref.store %value to %storage : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func nested @nested_result(
    // CHECK-SAME: -> (i8 {test.result = "nested"})
    obelisk_sim.func nested @nested_result(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> (i8 {test.result = "nested"})
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 2 : i8
      obelisk_sim.ref.store %value to %storage : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func private @address_result(
    // CHECK-SAME: -> (i8 {test.result = "address"})
    obelisk_sim.func private @address_result(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> (i8 {test.result = "address"})
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 3 : i8
      obelisk_sim.ref.store %value to %storage : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func private @metadata_result(
    // CHECK-SAME: -> (i8 {test.result = "metadata"})
    obelisk_sim.func private @metadata_result(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> (i8 {test.result = "metadata"})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64,
                    obelisk_sim.future_boundary = true} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i8>
      %value = arith.constant 4 : i8
      obelisk_sim.ref.store %value to %storage : i8, !obelisk_sim.ref<i8>
      obelisk_sim.return %value : i8
    }

    // CHECK-LABEL: obelisk_sim.func @root(
    // CHECK: %{{.*}} = obelisk_sim.call @public_result(%arg0) {res_attrs = [{test.call_result = "public"}]} : (!obelisk_sim.context) -> i8
    // CHECK: %{{.*}} = obelisk_sim.call @nested_result(%arg0) {res_attrs = [{test.call_result = "nested"}]} : (!obelisk_sim.context) -> i8
    // CHECK: %{{.*}} = obelisk_sim.call @address_result(%arg0) {res_attrs = [{test.call_result = "address"}]} : (!obelisk_sim.context) -> i8
    // CHECK: %{{.*}} = obelisk_sim.call @metadata_result(%arg0) {res_attrs = [{test.call_result = "metadata"}]} : (!obelisk_sim.context) -> i8
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      %address = "arith.constant"()
          {test.address = @address_result, value = 0 : i8} : () -> i8
      %public = obelisk_sim.call @public_result(%ctx)
          {res_attrs = [{test.call_result = "public"}]}
          : (!obelisk_sim.context) -> i8
      %nested = obelisk_sim.call @nested_result(%ctx)
          {res_attrs = [{test.call_result = "nested"}]}
          : (!obelisk_sim.context) -> i8
      %taken = obelisk_sim.call @address_result(%ctx)
          {res_attrs = [{test.call_result = "address"}]}
          : (!obelisk_sim.context) -> i8
      %metadata = obelisk_sim.call @metadata_result(%ctx)
          {res_attrs = [{test.call_result = "metadata"}]}
          : (!obelisk_sim.context) -> i8
      obelisk_sim.return
    }
  }
}

// -----

module {
  obelisk_sim.design @unresolved_uses {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 function hierarchy "top.target"
    obelisk_sim.storage.decl 0 in 0 : i16 design

    // An opaque region may introduce an unknown symbol scope, so symbol-use
    // discovery must pin this otherwise-private ABI, including its result.
    // CHECK-LABEL: obelisk_sim.design @unresolved_uses
    // CHECK-LABEL: obelisk_sim.func private @target(
    // CHECK-SAME: -> (i16 {test.result = "unresolved"})
    obelisk_sim.func private @target(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        -> (i16 {test.result = "unresolved"})
        attributes {entry_kind = 8 : i32, code_unit_id = 1 : i64} {
      %storage = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i16>
      %value = arith.constant 5 : i16
      obelisk_sim.ref.store %value to %storage : i16, !obelisk_sim.ref<i16>
      obelisk_sim.return %value : i16
    }

    // CHECK-LABEL: obelisk_sim.func @root(
    // CHECK: %{{.*}} = obelisk_sim.call @target(%arg0) {res_attrs = [{test.call_result = "unresolved"}]} : (!obelisk_sim.context) -> i16
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32} {
      "mystery.scope"() ({
      }) : () -> ()
      %result = obelisk_sim.call @target(%ctx)
          {res_attrs = [{test.call_result = "unresolved"}]}
          : (!obelisk_sim.context) -> i16
      obelisk_sim.return
    }
  }
}
