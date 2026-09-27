// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2017 30.4.7 says path polarity does not modify functional data
// propagation. It also does not exchange the path delay banks: delay
// selection follows the destination transition. These hand-authored actors
// model the legal positive `+=>` buffer and negative `-=>` inverter cases.

!logic1 = !obelisk.integral<1, false, true, 0 : 0, logic>

module {
  obelisk_sim.design @specify_path_polarity_lowering {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 9914001 in 0 continuous
        hierarchy "specify_path_polarity_lowering.positive"
    obelisk_sim.code_unit.decl 9914002 in 0 continuous
        hierarchy "specify_path_polarity_lowering.negative"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 1 in 0 drives 1 : !obelisk_sim.logic<1> design

    obelisk_sim.func @positive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9914001 : i64,
                    schedule.primitive_name = "buf",
                    obelisk_sim.propagation_delays = array<i64: 2, 3, 4>,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.positive", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 1 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 2 : i64, referenced_path = "top.positive",
            referenced_symbol = @positive_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 3 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {
          node_id = 4 : i64, referenced_path = "top.source",
          referenced_symbol = @source, semantic_type = !logic1} {}
      obelisk_sim.return
    }

    obelisk_sim.func @negative(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %out: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %source: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9914002 : i64,
                    schedule.primitive_name = "not",
                    obelisk_sim.propagation_delays = array<i64: 5, 6, 7>,
                    obelisk_sim.bindings = [
                      #obelisk_sim.argument_binding<path = "top.negative", argument = 1, kind = lvalue_only, copyOut = false>,
                      #obelisk_sim.argument_binding<path = "top.source", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.expression.assignment attributes {
          assignment_kind = 0 : i32, node_id = 11 : i64,
          semantic_type = !logic1} {
        obelisk.sv.expression.named_value attributes {
            node_id = 12 : i64, referenced_path = "top.negative",
            referenced_symbol = @negative_out, semantic_type = !logic1} {}
        obelisk.sv.expression.empty_argument attributes {
            node_id = 13 : i64, semantic_type = !logic1} {}
      }
      obelisk.sv.expression.named_value attributes {
          node_id = 14 : i64, referenced_path = "top.source",
          referenced_symbol = @source, semantic_type = !logic1} {}
      obelisk_sim.return
    }
  }
}

// CHECK-LABEL: obelisk_sim.func @positive
// CHECK: %[[POSITIVE:.*]] = obelisk_sim.logic.binary and
// CHECK-DAG: %[[PR:.*]] = obelisk_sim.time.constant 2
// CHECK-DAG: %[[PF:.*]] = obelisk_sim.time.constant 3
// CHECK-DAG: %[[PZ:.*]] = obelisk_sim.time.constant 4
// CHECK: obelisk_sim.driver.drive_inertial %arg1 = %[[POSITIVE]] after[%[[PR]], %[[PF]], %[[PZ]]]

// CHECK-LABEL: obelisk_sim.func @negative
// CHECK: %[[NEGATIVE:.*]] = obelisk_sim.logic.unary bit_not
// CHECK-DAG: %[[NR:.*]] = obelisk_sim.time.constant 5
// CHECK-DAG: %[[NF:.*]] = obelisk_sim.time.constant 6
// CHECK-DAG: %[[NZ:.*]] = obelisk_sim.time.constant 7
// CHECK: obelisk_sim.driver.drive_inertial %arg1 = %[[NEGATIVE]] after[%[[NR]], %[[NF]], %[[NZ]]]
