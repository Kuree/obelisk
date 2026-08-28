// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 30.4.4.1 uses only a module-path condition's LSB and treats
// X/Z as true. This hand-authored observer verifies the Clause-30-specific
// conversion without involving SystemVerilog parsing or runtime lowering.

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  obelisk_sim.design @specify_condition_truth {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9970001 in 0 observer
        hierarchy "specify_condition_truth.condition"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design

    obelisk_sim.func private @condition(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: !obelisk_sim.net<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 0 : i64}) -> i1
        attributes {
          entry_kind = 14 : i32, code_unit_id = 9970001 : i64,
          obelisk_sim.observer_result = 2 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.condition", argument = 1, kind = direct, copyOut = false>]}
        {
      obelisk.sv.expression.named_value attributes {
          node_id = 1 : i64, referenced_path = "top.condition",
          referenced_symbol = @condition, semantic_type = !logic4,
          obelisk.timing_path_condition_truth} {}
      %dummy = arith.constant false
      obelisk_sim.return %dummy : i1
    }
  }
}

// CHECK-LABEL: obelisk_sim.func private @condition
// CHECK: %[[VALUE:.*]] = obelisk_sim.net.read %arg1
// CHECK: %[[LSB:.*]] = obelisk_sim.logic.extract %[[VALUE]] from 0
// CHECK: %[[ZERO:.*]] = obelisk_sim.logic.constant false, false
// CHECK: %[[IS_ZERO:.*]] = obelisk_sim.logic.compare case_eq %[[LSB]], %[[ZERO]]
// CHECK: %[[ACTIVE:.*]] = arith.xori %[[IS_ZERO]], %true
// CHECK: obelisk_sim.return %[[ACTIVE]]
// CHECK-NOT: obelisk_sim.logic.is_true
