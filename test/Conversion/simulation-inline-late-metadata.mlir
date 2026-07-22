// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 missed-remarks=true}))' 2> %t.remarks | FileCheck %s
// RUN: FileCheck %s --check-prefix=REMARK < %t.remarks
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(inline)' | FileCheck %s --check-prefix=STOCK

module {
  obelisk_sim.design @late_metadata {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.late_metadata.callee.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.late_metadata.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func private @callee(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {effect_summary = [], entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      obelisk_sim.return %value : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = obelisk_sim.call @callee(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }
  }

  obelisk_sim.design @late_fragment_abi {
    obelisk_sim.code_unit.decl 9000011 in 0 function hierarchy "test.late_fragment.callee"
    obelisk_sim.code_unit.decl 9000012 in 0 function hierarchy "test.late_fragment.caller"
    obelisk_sim.scope.decl 0
    obelisk_sim.func private @callee(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000011 : i64, entry_kind = 8 : i32,
                    fragment_abi = #obelisk_sim.fragment_abi<version = 1, fragments = []>} {
      obelisk_sim.return %value : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000012 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @callee(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }
  }

  obelisk_sim.design @late_site {
    obelisk_sim.code_unit.decl 9000021 in 0 function hierarchy "test.late_site.callee"
    obelisk_sim.code_unit.decl 9000022 in 0 function hierarchy "test.late_site.caller"
    obelisk_sim.scope.decl 0
    obelisk_sim.func private @callee(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000021 : i64, entry_kind = 8 : i32} {
      %site = "arith.constant"() {
        test.site = #obelisk_sim.continuation<id = 1>, value = 0 : i32
      } : () -> i32
      obelisk_sim.return %site : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {code_unit_id = 9000022 : i64, entry_kind = 8 : i32} {
      %result = obelisk_sim.call @callee(%ctx, %value)
          : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %result : i32
    }
  }
}

// CHECK-LABEL: obelisk_sim.design @late_metadata
// CHECK: obelisk_sim.func private @callee
// CHECK: obelisk_sim.call @callee
// CHECK-LABEL: obelisk_sim.design @late_fragment_abi
// CHECK: obelisk_sim.func private @callee
// CHECK: obelisk_sim.call @callee
// CHECK-LABEL: obelisk_sim.design @late_site
// CHECK: obelisk_sim.func private @callee
// CHECK: obelisk_sim.call @callee
// REMARK-COUNT-3: not inlined: compute-graph or compiled-site metadata already exists
// STOCK-LABEL: obelisk_sim.design @late_metadata
// STOCK: obelisk_sim.func private @callee
// STOCK: obelisk_sim.call @callee
// STOCK-LABEL: obelisk_sim.design @late_fragment_abi
// STOCK: obelisk_sim.func private @callee
// STOCK: obelisk_sim.call @callee
// STOCK-LABEL: obelisk_sim.design @late_site
// STOCK: obelisk_sim.func private @callee
// STOCK: obelisk_sim.call @callee
