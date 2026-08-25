// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  obelisk_sim.design @partial_specify_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9930001 in 0 continuous
        hierarchy "partial_specify_lowering.path"
    obelisk_sim.code_unit.decl 9930002 in 0 observer
        hierarchy "partial_specify_lowering.condition"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design

    obelisk_sim.func private @condition(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %condition: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64}) -> i1
        attributes {entry_kind = 14 : i32, code_unit_id = 9930002 : i64,
                    obelisk_sim.lowered} {
      %value = obelisk_sim.net.read %condition :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %truth = obelisk_sim.logic.is_true %value : !obelisk_sim.logic<1>
      obelisk_sim.return %truth : i1
    }

    obelisk_sim.func @path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %snapshot: !obelisk_sim.ref<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %condition: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9930001 : i64,
                    obelisk_sim.timing_path_rules = [
                      {inputs = ["top.source"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 2>,
                       output_low = 0 : i64, output_width = 2 : i64,
                       output_root_width = 4 : i64,
                       connection_full = false,
                       polarity = 1 : i32, delays = array<i64: 2, 5, 7>,
                       condition_kind = 1 : i32,
                       condition_evaluator = @condition,
                       condition_captures = ["top.condition"],
                       condition_group = 0 : i32},
                      {inputs = ["top.source"],
                       snapshots = ["top.snapshot"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 2>,
                       output_low = 0 : i64, output_width = 2 : i64,
                       output_root_width = 4 : i64,
                       connection_full = false,
                       polarity = 2 : i32, delays = array<i64: 9>,
                       condition_kind = 2 : i32,
                       condition_group = 0 : i32},
                      {inputs = ["top.source", "top.source"],
                       snapshots = ["top.snapshot", "top.snapshot"],
                       input_lows = array<i64: 0, 3>,
                       input_widths = array<i64: 1, 1>,
                       output_low = 2 : i64, output_width = 2 : i64,
                       output_root_width = 4 : i64,
                       connection_full = true,
                       polarity = 0 : i32, delays = array<i64: 1, 4, 6>,
                       condition_kind = 0 : i32,
                       condition_group = 1 : i32}],
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.snapshot", argument = 3, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.condition", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic4} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.out",
            referenced_symbol = @out, semantic_type = !logic4} {}
        obelisk.sv.expression.named_value attributes {
            node_id = 3 : i64, referenced_path = "top.source",
            referenced_symbol = @source, semantic_type = !logic4} {}
      }
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @path
// CHECK-COUNT-1: obelisk_sim.logic.case_difference_mask
// CHECK-COUNT-1: obelisk_sim.ref.store
// CHECK: arith.shrui
// CHECK: arith.trunci
// CHECK: obelisk_sim.call @condition(%arg0, %arg4)
// CHECK: arith.xori
// CHECK: obelisk_sim.driver.drive_inertial_path
// CHECK-SAME: group 0 of 3
// CHECK: obelisk_sim.driver.drive_inertial_path
// CHECK-SAME: group 1 of 3
// CHECK: obelisk_sim.driver.drive_inertial_path
// CHECK-SAME: group 2 of 3
// CHECK: obelisk_sim.suspend.change %arg2
