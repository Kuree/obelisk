// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @conditional_delay {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "conditional_delay.bufif0"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @bufif0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out_low: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64, obelisk_sim.strength_driver_bank = 0 : i32},
        %out_high: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64, obelisk_sim.strength_driver_bank = 1 : i32},
        %data: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %control: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64,
                    obelisk_sim.primitive_name = "bufif0",
                    obelisk_sim.propagation_delays = array<i64: 2, 3, 4>,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.out", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.out", argument = 2, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.data", argument = 3, kind = direct, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.control", argument = 4, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 1 : i64, semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {node_id = 2 : i64, referenced_path = "top.out", referenced_symbol = @out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {node_id = 4 : i64, referenced_path = "top.data", referenced_symbol = @data, semantic_type = !logic1} {}
      obelisk.sv.expression.named_value attributes {node_id = 5 : i64, referenced_path = "top.control", referenced_symbol = @control, semantic_type = !logic1} {}
      obelisk_sim.return
    }
  }
}

// CHECK: error: conditional primitive propagation delays are not yet supported
