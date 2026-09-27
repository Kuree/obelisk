// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 30.4.4.1 uses only a module-path condition's LSB and treats
// X/Z as true. This hand-authored observer verifies the Clause-30-specific
// conversion without involving SystemVerilog parsing or runtime lowering.

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  simulation.design @specify_condition_truth {
    simulation.scope.decl 0
    simulation.code_unit.decl 9970001 in 0 observer
        hierarchy "specify_condition_truth.condition"
    simulation.net.decl 0 in 0 : !simulation.logic<4> design

    simulation.func private @condition(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %condition: !simulation.net<!simulation.logic<4>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 0 : i64}) -> i1
        attributes {
          entry_kind = 14 : i32, code_unit_id = 9970001 : i64,
          simulation.observer_result = 2 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.condition", argument = 1, kind = direct, copyOut = false>]}
        {
      obelisk.sv.expression.named_value attributes {
          node_id = 1 : i64, referenced_path = "top.condition",
          referenced_symbol = @condition, semantic_type = !logic4,
          obelisk.timing_path_condition_truth} {}
      %dummy = arith.constant false
      simulation.return %dummy : i1
    }
  }
}

// CHECK-LABEL: simulation.func private @condition
// CHECK: %[[VALUE:.*]] = simulation.net.read %arg1
// CHECK: %[[LSB:.*]] = simulation.logic.extract %[[VALUE]] from 0
// CHECK: %[[ZERO:.*]] = simulation.logic.constant false, false
// CHECK: %[[IS_ZERO:.*]] = simulation.logic.compare case_eq %[[LSB]], %[[ZERO]]
// CHECK: %[[ACTIVE:.*]] = arith.xori %[[IS_ZERO]], %true
// CHECK: simulation.return %[[ACTIVE]]
// CHECK-NOT: simulation.logic.is_true
