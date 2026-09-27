// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

!int = !obelisk.integral<32, true, false, 31 : 0, int>
!byte = !obelisk.integral<8, true, false, 7 : 0, byte>
!leaf = !obelisk.source_aggregate<"top", false, false, false, false, false,
    false, 0, 40, 40, 0, [
      {name = "number", ordinal = 0 : i32, packed_offset = 0 : i64,
       type = !int},
      {name = "octet", ordinal = 1 : i32, packed_offset = 0 : i64,
       type = !byte}
    ]>
!sim_leaf = !simulation.unpacked_struct<[
    #simulation.field<name = "number", type = i32, ordinal = 0,
        packedOffset = 0>,
    #simulation.field<name = "octet", type = i8, ordinal = 1,
        packedOffset = 0>
  ]>

module {
  simulation.design @assignment_pattern_type_setters {
    simulation.code_unit.decl 1 in 0 initial hierarchy "top"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !sim_leaf design
        hierarchy "top.value"

    // IEEE 1800-2017 10.9.1 and 10.9.2: the last matching type setter
    // supplies each uncovered element. This is a handwritten semantic-IR
    // test, so it also verifies that type-key metadata survives independently
    // of the source frontend.
    // CHECK-LABEL: simulation.func @unit
    // CHECK: %[[TWO:.*]] = arith.constant 2 : i32
    // CHECK: %[[THREE:.*]] = arith.constant 3 : i8
    // CHECK: %[[LEAF:.*]] = simulation.aggregate.construct %[[TWO]], %[[THREE]]
    // CHECK: simulation.ref.store %[[LEAF]] to %arg1
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!sim_leaf>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 1 : i32,
          simulation.delay_scale = 1 : i64,
          simulation.hierarchical_name = "top",
          simulation.bindings = [
            #simulation.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 1 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            assignment_kind = 0 : i32, node_id = 2 : i64,
            semantic_type = !leaf} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.value",
              referenced_symbol = @value, semantic_type = !leaf} {
          }
          obelisk.sv.expression.structured_assignment_pattern attributes {
              has_default_setter = false, index_setter_count = 0 : i64,
              member_setter_count = 0 : i64,
              member_setter_ordinals = array<i64>, node_id = 4 : i64,
              semantic_type = !leaf, type_setter_count = 3 : i64,
              type_setter_types = [!int, !int, !byte]} {
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "1", is_declared_unsized = true,
                is_signed = true, node_id = 5 : i64,
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "2", is_declared_unsized = true,
                is_signed = true, node_id = 6 : i64,
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                constant_value = "8'sd3", is_signed = true,
                node_id = 7 : i64, semantic_type = !byte} {
            }
          }
        }
      }
      simulation.return
    }
  }
}
