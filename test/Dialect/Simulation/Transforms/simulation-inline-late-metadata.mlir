// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 missed-remarks=true}))' 2> %t.remarks | FileCheck %s
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(inline)' | FileCheck %s --check-prefix=STOCK

module {
  simulation.design @late_metadata {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.late_metadata.callee.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.late_metadata.caller.9000002"
    simulation.scope.decl 0
    simulation.func private @callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {effect_summary = [], entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      simulation.return %value : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = simulation.call @callee(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }
  }

  simulation.design @late_fragment_abi {
    simulation.code_unit.decl 9000011 in 0 function hierarchy "test.late_fragment.callee"
    simulation.code_unit.decl 9000012 in 0 function hierarchy "test.late_fragment.caller"
    simulation.scope.decl 0
    simulation.func private @callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000011 : i64, entry_kind = 8 : i32,
                    fragment_abi = #schedule.fragment_abi<version = 1, fragments = []>} {
      simulation.return %value : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000012 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @callee(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }
  }

  simulation.design @late_site {
    simulation.code_unit.decl 9000021 in 0 function hierarchy "test.late_site.callee"
    simulation.code_unit.decl 9000022 in 0 function hierarchy "test.late_site.caller"
    simulation.scope.decl 0
    simulation.func private @callee(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000021 : i64, entry_kind = 8 : i32} {
      %site = "arith.constant"() {
        test.site = #schedule.continuation<id = 1>, value = 0 : i32
      } : () -> i32
      simulation.return %site : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000022 : i64, entry_kind = 8 : i32} {
      %result = simulation.call @callee(%ctx, %value)
          : (!simulation.context, i32) -> i32
      simulation.return %result : i32
    }
  }
}

// CHECK-LABEL: simulation.design @late_metadata
// CHECK: simulation.func private @callee
// CHECK: simulation.call @callee
// CHECK-LABEL: simulation.design @late_fragment_abi
// CHECK: simulation.func private @callee
// CHECK: simulation.call @callee
// CHECK-LABEL: simulation.design @late_site
// CHECK: simulation.func private @callee
// CHECK: simulation.call @callee
// REMARK-COUNT-3: not inlined: compute-graph or compiled-site metadata already exists
// STOCK-LABEL: simulation.design @late_metadata
// STOCK: simulation.func private @callee
// STOCK: simulation.call @callee
// STOCK-LABEL: simulation.design @late_fragment_abi
// STOCK: simulation.func private @callee
// STOCK: simulation.call @callee
// STOCK-LABEL: simulation.design @late_site
// STOCK: simulation.func private @callee
// STOCK: simulation.call @callee
