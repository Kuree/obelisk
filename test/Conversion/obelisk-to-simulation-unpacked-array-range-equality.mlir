// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!left_array = !obelisk.ranged_unpacked_array<7 : 4 x !bit>
!right_array = !obelisk.ranged_unpacked_array<3 : 0 x !bit>
!logic = !obelisk.integral<1, false, true, 0 : 0, logic>
!logic_left_array = !obelisk.ranged_unpacked_array<7 : 4 x !logic>
!logic_right_array = !obelisk.ranged_unpacked_array<3 : 0 x !logic>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!descending_array = !obelisk.ranged_unpacked_array<3 : 0 x !int>
!ascending_array = !obelisk.ranged_unpacked_array<0 : 3 x !int>

module {
  simulation.design @range_equality {
    simulation.code_unit.decl 9200001 in 0 initial
        hierarchy "test.range_equality.9200001"
    simulation.code_unit.decl 9200002 in 0 function
        hierarchy "test.range_equality.logic.9200002"
    simulation.code_unit.decl 9200003 in 0 function
        hierarchy "test.range_assignment.9200003"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 :
        !simulation.unpacked_array<7 : 4 x i1> design hierarchy "top.left"
    simulation.storage.decl 1 in 0 :
        !simulation.unpacked_array<3 : 0 x i1> design hierarchy "top.right"
    simulation.storage.decl 2 in 0 : i1 design hierarchy "top.result"
    simulation.storage.decl 3 in 0 :
        !simulation.unpacked_array<7 : 4 x !simulation.logic<1>>
        design hierarchy "top.logic_left"
    simulation.storage.decl 4 in 0 :
        !simulation.unpacked_array<3 : 0 x !simulation.logic<1>>
        design hierarchy "top.logic_right"
    simulation.storage.decl 5 in 0 :
        !simulation.unpacked_array<3 : 0 x i32>
        design hierarchy "top.descending"
    simulation.storage.decl 6 in 0 :
        !simulation.unpacked_array<0 : 3 x i32>
        design hierarchy "top.ascending"

    // CHECK-LABEL: simulation.func @unit
    // CHECK: %[[NORMALIZED:.*]] = simulation.aggregate.construct
    // CHECK-SAME: -> !simulation.unpacked_array<7 : 4 x i1>
    // CHECK: %[[LEFT_ELEMENT:.*]] = simulation.aggregate.extract %{{.*}}[0]
    // CHECK: %[[RIGHT_ELEMENT:.*]] = simulation.aggregate.extract %[[NORMALIZED]][0]
    // CHECK: arith.cmpi eq, %[[LEFT_ELEMENT]], %[[RIGHT_ELEMENT]]
    // CHECK: %[[RESULT:.*]] = simulation.logic.to_bits
    // CHECK: simulation.ref.store %[[RESULT]] to %arg3
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %left: !simulation.ref<!simulation.unpacked_array<7 : 4 x i1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %right: !simulation.ref<!simulation.unpacked_array<3 : 0 x i1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %result: !simulation.ref<i1>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64})
        attributes {
          entry_kind = 1 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.left", argument = 1,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.right", argument = 2,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.result", argument = 3,
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
      simulation.return
    }

    // All four unpacked aggregate equality operators preserve their distinct
    // four-state semantics after ordinal range normalization.
    // CHECK-LABEL: simulation.func @logic_equality
    // CHECK: simulation.logic.compare eq
    // CHECK: simulation.logic.compare eq
    // CHECK: simulation.logic.unary logical_not
    // CHECK: simulation.logic.compare case_eq
    // CHECK: arith.andi
    // CHECK: simulation.logic.compare case_eq
    // CHECK: arith.xori
    simulation.func @logic_equality(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %left: !simulation.ref<
            !simulation.unpacked_array<7 : 4 x !simulation.logic<1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64},
        %right: !simulation.ref<
            !simulation.unpacked_array<3 : 0 x !simulation.logic<1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 4 : i64})
        attributes {
          entry_kind = 8 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.logic_left",
                argument = 1, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.logic_right",
                argument = 2, kind = direct, copyOut = false>
          ],
          code_unit_id = 9200002 : i64,
          simulation.void_function
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
        obelisk.sv.expression.binary_op attributes {
            node_id = 11 : i64, operator_kind = 9 : i32,
            semantic_type = !logic} {
          obelisk.sv.expression.named_value attributes {
              node_id = 12 : i64, referenced_path = "top.logic_left",
              referenced_symbol = @logic_left,
              semantic_type = !logic_left_array} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 13 : i64, referenced_path = "top.logic_right",
              referenced_symbol = @logic_right,
              semantic_type = !logic_right_array} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
        obelisk.sv.expression.binary_op attributes {
            node_id = 15 : i64, operator_kind = 10 : i32,
            semantic_type = !logic} {
          obelisk.sv.expression.named_value attributes {
              node_id = 16 : i64, referenced_path = "top.logic_left",
              referenced_symbol = @logic_left,
              semantic_type = !logic_left_array} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 17 : i64, referenced_path = "top.logic_right",
              referenced_symbol = @logic_right,
              semantic_type = !logic_right_array} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 18 : i64} {
        obelisk.sv.expression.binary_op attributes {
            node_id = 19 : i64, operator_kind = 11 : i32,
            semantic_type = !bit} {
          obelisk.sv.expression.named_value attributes {
              node_id = 20 : i64, referenced_path = "top.logic_left",
              referenced_symbol = @logic_left,
              semantic_type = !logic_left_array} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 21 : i64, referenced_path = "top.logic_right",
              referenced_symbol = @logic_right,
              semantic_type = !logic_right_array} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
        obelisk.sv.expression.binary_op attributes {
            node_id = 23 : i64, operator_kind = 12 : i32,
            semantic_type = !bit} {
          obelisk.sv.expression.named_value attributes {
              node_id = 24 : i64, referenced_path = "top.logic_left",
              referenced_symbol = @logic_left,
              semantic_type = !logic_left_array} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 25 : i64, referenced_path = "top.logic_right",
              referenced_symbol = @logic_right,
              semantic_type = !logic_right_array} {
          }
        }
      }
      simulation.return
    }

    // Assignment across opposite declared directions pairs elements by
    // ordinal position and reconstructs the destination's declared range.
    // CHECK-LABEL: simulation.func @range_assignment
    // CHECK: %[[SOURCE:.*]] = simulation.ref.load %arg1
    // CHECK: %[[E0:.*]] = simulation.aggregate.extract %[[SOURCE]][0]
    // CHECK: %[[E1:.*]] = simulation.aggregate.extract %[[SOURCE]][1]
    // CHECK: %[[E2:.*]] = simulation.aggregate.extract %[[SOURCE]][2]
    // CHECK: %[[E3:.*]] = simulation.aggregate.extract %[[SOURCE]][3]
    // CHECK: %[[NORMALIZED:.*]] = simulation.aggregate.construct
    // CHECK-SAME: %[[E0]], %[[E1]], %[[E2]], %[[E3]]
    // CHECK-SAME: -> !simulation.unpacked_array<0 : 3 x i32>
    // CHECK: simulation.ref.store %[[NORMALIZED]] to %arg2
    simulation.func @range_assignment(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %descending: !simulation.ref<
            !simulation.unpacked_array<3 : 0 x i32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 5 : i64},
        %ascending: !simulation.ref<
            !simulation.unpacked_array<0 : 3 x i32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 6 : i64})
        attributes {
          entry_kind = 8 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.descending",
                argument = 1, kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.ascending",
                argument = 2, kind = direct, copyOut = false>
          ],
          code_unit_id = 9200003 : i64,
          simulation.void_function
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 31 : i64, assignment_kind = 0 : i32,
            semantic_type = !ascending_array} {
          obelisk.sv.expression.named_value attributes {
              node_id = 32 : i64, referenced_path = "top.ascending",
              referenced_symbol = @ascending,
              semantic_type = !ascending_array} {
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 33 : i64, referenced_path = "top.descending",
              referenced_symbol = @descending,
              semantic_type = !descending_array} {
          }
        }
      }
      simulation.return
    }
  }
}
