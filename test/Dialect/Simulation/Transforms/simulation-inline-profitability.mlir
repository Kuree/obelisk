// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=1 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O1
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=2 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O2
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O3
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=0 caller-growth-constant=0 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=CALLER-BUDGET
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=0 caller-growth-constant=1 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=CALLER-EXACT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=0 design-growth-constant=0}))' | FileCheck %s --check-prefix=DESIGN-BUDGET
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=0 design-growth-constant=1}))' | FileCheck %s --check-prefix=CUMULATIVE

module {
  obelisk_sim.design @presets {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.presets.cost8.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.presets.cost9.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 function hierarchy "test.presets.cost12.9000003"
    obelisk_sim.code_unit.decl 9000004 in 0 function hierarchy "test.presets.cost13.9000004"
    obelisk_sim.code_unit.decl 9000005 in 0 function hierarchy "test.presets.cost24.9000005"
    obelisk_sim.code_unit.decl 9000006 in 0 function hierarchy "test.presets.cost25.9000006"
    obelisk_sim.code_unit.decl 9000007 in 0 function hierarchy "test.presets.constant_specialization.9000007"
    obelisk_sim.code_unit.decl 9000008 in 0 function hierarchy "test.presets.descriptor_specialization.9000008"
    obelisk_sim.code_unit.decl 9000009 in 0 function hierarchy "test.presets.preset_caller.9000009"
    obelisk_sim.code_unit.decl 9000010 in 0 function hierarchy "test.presets.specialization_caller.9000010"
    obelisk_sim.code_unit.decl 9000011 in 0 function hierarchy "test.presets.descriptor_caller.9000011"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design

    // Two weighted state reads plus two ordinary operations cost exactly 8.
    obelisk_sim.func private @cost8(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      obelisk_sim.return %b : !obelisk_sim.logic<8>
    }

    obelisk_sim.func private @cost9(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %c : !obelisk_sim.logic<8>
    }

    obelisk_sim.func private @cost12(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %d = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %d : !obelisk_sim.logic<8>
    }

    obelisk_sim.func private @cost13(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %d = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %extra = arith.constant 0 : i32
      obelisk_sim.return %d : !obelisk_sim.logic<8>
    }

    obelisk_sim.func private @cost24(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %d = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %e = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %f = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %g = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %h = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %h : !obelisk_sim.logic<8>
    }

    obelisk_sim.func private @cost25(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %d = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %e = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %f = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %g = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %h = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %extra = arith.constant 0 : i32
      obelisk_sim.return %h : !obelisk_sim.logic<8>
    }

    // Cost 13 is above O2's tiny threshold but the constant actual makes this
    // a specialization candidate below O2's cost-48 ceiling.
    obelisk_sim.func private @constant_specialization(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000007 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      %c6 = arith.constant 6 : i32
      %c7 = arith.constant 7 : i32
      %c8 = arith.constant 8 : i32
      %c9 = arith.constant 9 : i32
      %c10 = arith.constant 10 : i32
      %c11 = arith.constant 11 : i32
      %sum = arith.addi %value, %c1 : i32
      obelisk_sim.return %sum : i32
    }

    // Five state reads cost 15. A context-derived handle supplies concrete
    // descriptor provenance and therefore enables O2 specialization.
    obelisk_sim.func private @descriptor_specialization(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %a = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %b = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %c = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %d = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %e = obelisk_sim.ref.load %ref : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %e : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @preset_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %ref: !obelisk_sim.ref<!obelisk_sim.logic<8>> {obelisk_sim.capture_kind = 2 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000009 : i64} {
      %v8 = obelisk_sim.call @cost8(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      %v9 = obelisk_sim.call @cost9(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      %v12 = obelisk_sim.call @cost12(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      %v13 = obelisk_sim.call @cost13(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      %v24 = obelisk_sim.call @cost24(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      %v25 = obelisk_sim.call @cost25(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      obelisk_sim.return %v25 : !obelisk_sim.logic<8>
    }

    obelisk_sim.func @specialization_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000010 : i64} {
      %forty_one = arith.constant 41 : i32
      %answer = obelisk_sim.call @constant_specialization(%ctx, %forty_one) : (!obelisk_sim.context, i32) -> i32
      obelisk_sim.return %answer : i32
    }

    obelisk_sim.func @descriptor_caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000011 : i64} {
      %ref = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.call @descriptor_specialization(%ctx, %ref) : (!obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> !obelisk_sim.logic<8>
      obelisk_sim.return %value : !obelisk_sim.logic<8>
    }
  }

  obelisk_sim.design @caller_budget {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.caller_budget.cost6.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.caller_budget.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func private @cost6(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      obelisk_sim.return %c5 : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = obelisk_sim.call @cost6(%ctx) : (!obelisk_sim.context) -> i32
      obelisk_sim.return %result : i32
    }
  }

  obelisk_sim.design @design_budget {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.design_budget.cost6.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.design_budget.caller.9000002"
    obelisk_sim.scope.decl 0
    // Public visibility retains this callee, so replacing a cost-five call
    // with its cost-six body consumes one unit of whole-design budget.
    obelisk_sim.func @cost6(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      obelisk_sim.return %c5 : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = obelisk_sim.call @cost6(%ctx) : (!obelisk_sim.context) -> i32
      obelisk_sim.return %result : i32
    }
  }

  obelisk_sim.design @design_delete_budget {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.design_delete_budget.cost6.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.design_delete_budget.caller.9000002"
    obelisk_sim.scope.decl 0
    // This single-use private callee is erased. The design therefore shrinks
    // by the removed call cost and must fit a zero-growth design budget.
    obelisk_sim.func private @cost6(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      obelisk_sim.return %c5 : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = obelisk_sim.call @cost6(%ctx) : (!obelisk_sim.context) -> i32
      obelisk_sim.return %result : i32
    }
  }

  obelisk_sim.design @cumulative_design_budget {
    obelisk_sim.code_unit.decl 9000001 in 0 function hierarchy "test.cumulative_design_budget.cost6.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 function hierarchy "test.cumulative_design_budget.caller.9000002"
    obelisk_sim.scope.decl 0
    obelisk_sim.func @cost6(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      obelisk_sim.return %c5 : i32
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %first = obelisk_sim.call @cost6(%ctx) : (!obelisk_sim.context) -> i32
      %second = obelisk_sim.call @cost6(%ctx) : (!obelisk_sim.context) -> i32
      %sum = arith.addi %first, %second : i32
      obelisk_sim.return %sum : i32
    }
  }
}

// O1-LABEL: obelisk_sim.func @preset_caller
// O1-NOT: obelisk_sim.call @cost8
// O1: obelisk_sim.call @cost9
// O1-LABEL: obelisk_sim.func @specialization_caller
// O1: obelisk_sim.call @constant_specialization
// O1-LABEL: obelisk_sim.func @descriptor_caller
// O1: obelisk_sim.call @descriptor_specialization

// O2-LABEL: obelisk_sim.func @preset_caller
// O2-NOT: obelisk_sim.call @cost{{(8|9|12)}}
// O2: obelisk_sim.call @cost13
// O2-LABEL: obelisk_sim.func @specialization_caller
// O2-NOT: obelisk_sim.call @constant_specialization
// O2-LABEL: obelisk_sim.func @descriptor_caller
// O2-NOT: obelisk_sim.call @descriptor_specialization

// O3-LABEL: obelisk_sim.func @preset_caller
// O3-NOT: obelisk_sim.call @cost{{(8|9|12|13|24)}}
// O3: obelisk_sim.call @cost25

// CALLER-BUDGET-LABEL: obelisk_sim.design @caller_budget
// CALLER-BUDGET: obelisk_sim.call @cost6
// CALLER-EXACT-LABEL: obelisk_sim.design @caller_budget
// CALLER-EXACT-NOT: obelisk_sim.call @cost6
// CALLER-EXACT-LABEL: obelisk_sim.design @design_budget

// DESIGN-BUDGET-LABEL: obelisk_sim.design @design_budget
// DESIGN-BUDGET: obelisk_sim.call @cost6
// DESIGN-BUDGET-LABEL: obelisk_sim.design @design_delete_budget
// DESIGN-BUDGET-NOT: obelisk_sim.call @cost6
// DESIGN-BUDGET-LABEL: obelisk_sim.design @cumulative_design_budget

// CUMULATIVE-LABEL: obelisk_sim.design @cumulative_design_budget
// Exactly one of the two calls consumes the single available growth unit.
// CUMULATIVE-COUNT-1: obelisk_sim.call @cost6
