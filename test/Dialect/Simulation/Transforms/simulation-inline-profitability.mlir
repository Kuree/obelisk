// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=1 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O1
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=2 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O2
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=O3
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=0 caller-growth-constant=0 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=CALLER-BUDGET
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=0 caller-growth-constant=1 design-growth-percent=10000 design-growth-constant=10000}))' | FileCheck %s --check-prefix=CALLER-EXACT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=0 design-growth-constant=0}))' | FileCheck %s --check-prefix=DESIGN-BUDGET
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-inline{opt-level=3 tiny-cost=100 specialization-cost=0 caller-growth-percent=10000 caller-growth-constant=10000 design-growth-percent=0 design-growth-constant=1}))' | FileCheck %s --check-prefix=CUMULATIVE

module {
  simulation.design @presets {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.presets.cost8.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.presets.cost9.9000002"
    simulation.code_unit.decl 9000003 in 0 function hierarchy "test.presets.cost12.9000003"
    simulation.code_unit.decl 9000004 in 0 function hierarchy "test.presets.cost13.9000004"
    simulation.code_unit.decl 9000005 in 0 function hierarchy "test.presets.cost24.9000005"
    simulation.code_unit.decl 9000006 in 0 function hierarchy "test.presets.cost25.9000006"
    simulation.code_unit.decl 9000007 in 0 function hierarchy "test.presets.constant_specialization.9000007"
    simulation.code_unit.decl 9000008 in 0 function hierarchy "test.presets.descriptor_specialization.9000008"
    simulation.code_unit.decl 9000009 in 0 function hierarchy "test.presets.preset_caller.9000009"
    simulation.code_unit.decl 9000010 in 0 function hierarchy "test.presets.specialization_caller.9000010"
    simulation.code_unit.decl 9000011 in 0 function hierarchy "test.presets.descriptor_caller.9000011"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<8> design

    // Two weighted state reads plus two ordinary operations cost exactly 8.
    simulation.func private @cost8(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      simulation.return %b : !simulation.logic<8>
    }

    simulation.func private @cost9(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %c : !simulation.logic<8>
    }

    simulation.func private @cost12(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000003 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %d = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %d : !simulation.logic<8>
    }

    simulation.func private @cost13(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000004 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %d = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %extra = arith.constant 0 : i32
      simulation.return %d : !simulation.logic<8>
    }

    simulation.func private @cost24(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000005 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %d = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %e = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %f = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %g = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %h = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %h : !simulation.logic<8>
    }

    simulation.func private @cost25(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000006 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %d = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %e = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %f = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %g = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %h = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %extra = arith.constant 0 : i32
      simulation.return %h : !simulation.logic<8>
    }

    // Cost 13 is above O2's tiny threshold but the constant actual makes this
    // a specialization candidate below O2's cost-48 ceiling.
    simulation.func private @constant_specialization(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32}) -> i32
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
      simulation.return %sum : i32
    }

    // Five state reads cost 15. A context-derived handle supplies concrete
    // descriptor provenance and therefore enables O2 specialization.
    simulation.func private @descriptor_specialization(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000008 : i64} {
      %a = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %b = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %c = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %d = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %e = simulation.ref.load %ref : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.return %e : !simulation.logic<8>
    }

    simulation.func @preset_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %ref: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 2 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000009 : i64} {
      %v8 = simulation.call @cost8(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      %v9 = simulation.call @cost9(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      %v12 = simulation.call @cost12(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      %v13 = simulation.call @cost13(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      %v24 = simulation.call @cost24(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      %v25 = simulation.call @cost25(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      simulation.return %v25 : !simulation.logic<8>
    }

    simulation.func @specialization_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000010 : i64} {
      %forty_one = arith.constant 41 : i32
      %answer = simulation.call @constant_specialization(%ctx, %forty_one) : (!simulation.context, i32) -> i32
      simulation.return %answer : i32
    }

    simulation.func @descriptor_caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> !simulation.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 9000011 : i64} {
      %ref = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<8>>
      %value = simulation.call @descriptor_specialization(%ctx, %ref) : (!simulation.context, !simulation.ref<!simulation.logic<8>>) -> !simulation.logic<8>
      simulation.return %value : !simulation.logic<8>
    }
  }

  simulation.design @caller_budget {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.caller_budget.cost6.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.caller_budget.caller.9000002"
    simulation.scope.decl 0
    simulation.func private @cost6(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      simulation.return %c5 : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = simulation.call @cost6(%ctx) : (!simulation.context) -> i32
      simulation.return %result : i32
    }
  }

  simulation.design @design_budget {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.design_budget.cost6.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.design_budget.caller.9000002"
    simulation.scope.decl 0
    // Public visibility retains this callee, so replacing a cost-five call
    // with its cost-six body consumes one unit of whole-design budget.
    simulation.func @cost6(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      simulation.return %c5 : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = simulation.call @cost6(%ctx) : (!simulation.context) -> i32
      simulation.return %result : i32
    }
  }

  simulation.design @design_delete_budget {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.design_delete_budget.cost6.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.design_delete_budget.caller.9000002"
    simulation.scope.decl 0
    // This single-use private callee is erased. The design therefore shrinks
    // by the removed call cost and must fit a zero-growth design budget.
    simulation.func private @cost6(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      simulation.return %c5 : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %result = simulation.call @cost6(%ctx) : (!simulation.context) -> i32
      simulation.return %result : i32
    }
  }

  simulation.design @cumulative_design_budget {
    simulation.code_unit.decl 9000001 in 0 function hierarchy "test.cumulative_design_budget.cost6.9000001"
    simulation.code_unit.decl 9000002 in 0 function hierarchy "test.cumulative_design_budget.caller.9000002"
    simulation.scope.decl 0
    simulation.func @cost6(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000001 : i64} {
      %c0 = arith.constant 0 : i32
      %c1 = arith.constant 1 : i32
      %c2 = arith.constant 2 : i32
      %c3 = arith.constant 3 : i32
      %c4 = arith.constant 4 : i32
      %c5 = arith.constant 5 : i32
      simulation.return %c5 : i32
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i32
        attributes {entry_kind = 8 : i32, code_unit_id = 9000002 : i64} {
      %first = simulation.call @cost6(%ctx) : (!simulation.context) -> i32
      %second = simulation.call @cost6(%ctx) : (!simulation.context) -> i32
      %sum = arith.addi %first, %second : i32
      simulation.return %sum : i32
    }
  }
}

// O1-LABEL: simulation.func @preset_caller
// O1-NOT: simulation.call @cost8
// O1: simulation.call @cost9
// O1-LABEL: simulation.func @specialization_caller
// O1: simulation.call @constant_specialization
// O1-LABEL: simulation.func @descriptor_caller
// O1: simulation.call @descriptor_specialization

// O2-LABEL: simulation.func @preset_caller
// O2-NOT: simulation.call @cost{{(8|9|12)}}
// O2: simulation.call @cost13
// O2-LABEL: simulation.func @specialization_caller
// O2-NOT: simulation.call @constant_specialization
// O2-LABEL: simulation.func @descriptor_caller
// O2-NOT: simulation.call @descriptor_specialization

// O3-LABEL: simulation.func @preset_caller
// O3-NOT: simulation.call @cost{{(8|9|12|13|24)}}
// O3: simulation.call @cost25

// CALLER-BUDGET-LABEL: simulation.design @caller_budget
// CALLER-BUDGET: simulation.call @cost6
// CALLER-EXACT-LABEL: simulation.design @caller_budget
// CALLER-EXACT-NOT: simulation.call @cost6
// CALLER-EXACT-LABEL: simulation.design @design_budget

// DESIGN-BUDGET-LABEL: simulation.design @design_budget
// DESIGN-BUDGET: simulation.call @cost6
// DESIGN-BUDGET-LABEL: simulation.design @design_delete_budget
// DESIGN-BUDGET-NOT: simulation.call @cost6
// DESIGN-BUDGET-LABEL: simulation.design @cumulative_design_budget

// CUMULATIVE-LABEL: simulation.design @cumulative_design_budget
// Exactly one of the two calls consumes the single available growth unit.
// CUMULATIVE-COUNT-1: simulation.call @cost6
