// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic2 = !obelisk.integral<2, false, true, 1 : 0, logic>
!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  simulation.design @split_specify_lowering {
    simulation.scope.decl 0
    simulation.code_unit.decl 9940001 in 0 continuous
        hierarchy "split_specify_lowering.path"
    simulation.storage.decl 0 in 0 : !simulation.logic<4> design
    simulation.storage.decl 1 in 0 : !simulation.logic<4> design
    simulation.net.decl 0 in 0 : !simulation.logic<2> design
    simulation.net.decl 1 in 0 : !simulation.logic<2> design
    simulation.net.decl 2 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<2> design
    simulation.driver.decl 1 in 0 drives 1 : !simulation.logic<2> design

    simulation.func @path(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %low: !simulation.driver<!simulation.logic<2>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %high: !simulation.driver<!simulation.logic<2>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 1 : i64},
        %source: !simulation.net<!simulation.logic<4>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 2 : i64},
        %snapshot0: !simulation.ref<!simulation.logic<4>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %snapshot1: !simulation.ref<!simulation.logic<4>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9940001 : i64,
                    simulation.timing_path_rules = [
                      {inputs = ["top.source"],
                       snapshots = ["top.snapshot0"],
                       input_lows = array<i64: 0>,
                       input_widths = array<i64: 2>,
                       output_low = 0 : i64, output_width = 2 : i64,
                       output_root_width = 2 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = false,
                       polarity = 0 : i32,
                       delays = array<i64: 1, 2, 3, 4, 5, 6>,
                       condition_kind = 0 : i32,
                       condition_group = 0 : i32},
                      {inputs = ["top.source"],
                       snapshots = ["top.snapshot1"],
                       input_lows = array<i64: 2>,
                       input_widths = array<i64: 2>,
                       output_low = 0 : i64, output_width = 2 : i64,
                       output_root_width = 2 : i64,
                       driver_node_id = 3 : i64,
                       connection_full = false,
                       polarity = 0 : i32,
                       delays = array<i64: 12, 11, 10, 9, 8, 7,
                                                   6, 5, 4, 3, 2, 1>,
                       condition_kind = 0 : i32,
                       condition_group = 1 : i32}],
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.low", argument = 1, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.high", argument = 2, kind = lvalue_only, copyOut = false>,
                      #simulation.argument_binding<path = "top.source", argument = 3, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot0", argument = 4, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.snapshot1", argument = 5, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic4} {
        obelisk.sv.expression.concatenation attributes {
            node_id = 10 : i64, semantic_type = !logic4} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.high",
              referenced_symbol = @high, semantic_type = !logic2} {}
          obelisk.sv.expression.named_value attributes {
              node_id = 2 : i64, referenced_path = "top.low",
              referenced_symbol = @low, semantic_type = !logic2} {}
        }
        obelisk.sv.expression.named_value attributes {
            node_id = 4 : i64, referenced_path = "top.source",
            referenced_symbol = @source, semantic_type = !logic4} {}
      }
      simulation.return
    }
  }
}

// One actor computes both change masks before publishing either leaf. Each
// leaf selects only its own driver-local plan. The six-value rule derives six
// distinct static delays; the twelve-value rule retains all twelve.
// CHECK-LABEL: simulation.func @path
// CHECK-COUNT-2: simulation.logic.case_difference_mask
// CHECK: simulation.driver.read
// CHECK: site 9940001 : {{[01]}} group 0 of 12
// CHECK: site 9940001 : {{[01]}} group 11 of 12
// CHECK: simulation.driver.read
// CHECK: site 9940001 : {{[01]}} group 0 of 6
// CHECK: site 9940001 : {{[01]}} group 5 of 6
// CHECK: simulation.suspend.change %arg3
