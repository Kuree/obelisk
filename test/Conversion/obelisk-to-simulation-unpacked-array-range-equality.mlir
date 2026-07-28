// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!left_array = !obelisk.ranged_unpacked_array<7 : 4 x !bit>
!right_array = !obelisk.ranged_unpacked_array<3 : 0 x !bit>

module {
  obelisk_sim.design @range_equality {
    obelisk_sim.code_unit.decl 9200001 in 0 initial
        hierarchy "test.range_equality.9200001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<7 : 4 x i1> design hierarchy "top.left"
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.unpacked_array<3 : 0 x i1> design hierarchy "top.right"
    obelisk_sim.storage.decl 2 in 0 : i1 design hierarchy "top.result"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: %[[NORMALIZED:.*]] = obelisk_sim.aggregate.construct
    // CHECK-SAME: -> !obelisk_sim.unpacked_array<7 : 4 x i1>
    // CHECK: %[[LEFT_ELEMENT:.*]] = obelisk_sim.aggregate.extract %{{.*}}[0]
    // CHECK: %[[RIGHT_ELEMENT:.*]] = obelisk_sim.aggregate.extract %[[NORMALIZED]][0]
    // CHECK: arith.cmpi eq, %[[LEFT_ELEMENT]], %[[RIGHT_ELEMENT]]
    // CHECK: %[[RESULT:.*]] = obelisk_sim.logic.to_bits
    // CHECK: obelisk_sim.ref.store %[[RESULT]] to %arg3
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %left: !obelisk_sim.ref<!obelisk_sim.unpacked_array<7 : 4 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %right: !obelisk_sim.ref<!obelisk_sim.unpacked_array<3 : 0 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %result: !obelisk_sim.ref<i1>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.left", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.right", argument = 2,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.result", argument = 3,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9200001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !bit} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.result",
              referenced_symbol = @result, semantic_type = !bit} {
          }
          obelisk.sv.expression.binary_op attributes {
              node_id = 4 : i64, operator_kind = 9 : i32,
              semantic_type = !bit} {
            obelisk.sv.expression.named_value attributes {
                node_id = 5 : i64, referenced_path = "top.left",
                referenced_symbol = @left, semantic_type = !left_array} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 6 : i64, referenced_path = "top.right",
                referenced_symbol = @right, semantic_type = !right_array} {
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}
