// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!logic2 = !obelisk.integral<2, false, true, 1 : 0, logic>
!logic4 = !obelisk.integral<4, false, true, 3 : 0, logic>

module {
  obelisk_sim.design @split_specify_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9940001 in 0 continuous
        hierarchy "split_specify_lowering.path"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<2> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<2> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<2> design

    obelisk_sim.func @path(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %low: !obelisk_sim.driver<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %high: !obelisk_sim.driver<!obelisk_sim.logic<2>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %snapshot0: !obelisk_sim.ref<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %snapshot1: !obelisk_sim.ref<!obelisk_sim.logic<4>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9940001 : i64,
                    obelisk_sim.timing_path_rules = [
                      {inputs = ["top.source"],
                       snapshots = ["top.snapshot0"],
                       input_lows = array<i64: 0>,
                       input_widths = array<i64: 2>,
                       output_low = 0 : i64, output_width = 2 : i64,
                       output_root_width = 2 : i64,
                       driver_node_id = 2 : i64,
                       connection_full = false,
                       polarity = 0 : i32, delays = array<i64: 2>,
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
                       polarity = 0 : i32, delays = array<i64: 5>,
                       condition_kind = 0 : i32,
                       condition_group = 1 : i32}],
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.low", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.high", argument = 2, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.source", argument = 3, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.snapshot0", argument = 4, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.snapshot1", argument = 5, kind = direct, copyOut = false>]} {
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
      obelisk_sim.return
    }
  }
}

// One actor computes both change masks before publishing either leaf. Each
// leaf selects only its own driver-local plan and delay bank.
// CHECK-LABEL: obelisk_sim.func @path
// CHECK-COUNT-2: obelisk_sim.logic.case_difference_mask
// CHECK-DAG: after[%{{.*}}, %{{.*}}, %{{.*}}] site 9940001 : 0 group 0 of 1
// CHECK-DAG: after[%{{.*}}, %{{.*}}, %{{.*}}] site 9940001 : 1 group 0 of 1
// CHECK: obelisk_sim.suspend.change %arg3
