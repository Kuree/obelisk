// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Drives every indexed part-select direction combination directly from
// hand-authored MLIR. The second child is the select width, not a second bound.
// Both source declaration directions and both indexed directions must map the
// selected source indices to the same physical low bit.

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!descending = !obelisk.ranged_packed_array<7 : 0 x !bit>
!descending_slice = !obelisk.ranged_packed_array<6 : 4 x !bit>
!ascending = !obelisk.ranged_packed_array<0 : 7 x !bit>
!ascending_slice = !obelisk.ranged_packed_array<3 : 5 x !bit>

module {
  obelisk_sim.design @indexed_part_select {
    obelisk_sim.code_unit.decl 9500001 in 0 initial
        hierarchy "top.indexed_part_select"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.packed_array<7 : 0 x i1>
        design hierarchy "top.descending"
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.packed_array<0 : 7 x i1>
        design hierarchy "top.ascending"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: %[[ONES:.*]] = arith.constant -1 : i3
    // CHECK: %[[PACKED0:.*]] = obelisk_sim.packed.unflatten %[[ONES]]
    // CHECK: %[[DESC_UP:.*]] = obelisk_sim.ref.extract %arg1 from 4
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x i1>>
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<6 : 4 x i1>>
    // CHECK: obelisk_sim.ref.store %[[PACKED0]] to %[[DESC_UP]]
    // CHECK: %[[PACKED1:.*]] = obelisk_sim.packed.unflatten
    // CHECK: %[[DESC_DOWN:.*]] = obelisk_sim.ref.extract %arg1 from 4
    // CHECK: obelisk_sim.ref.store %[[PACKED1]] to %[[DESC_DOWN]]
    // CHECK: %[[PACKED2:.*]] = obelisk_sim.packed.unflatten
    // CHECK: %[[ASC_UP:.*]] = obelisk_sim.ref.extract %arg2 from 2
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<0 : 7 x i1>>
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<3 : 5 x i1>>
    // CHECK: obelisk_sim.ref.store %[[PACKED2]] to %[[ASC_UP]]
    // CHECK: %[[PACKED3:.*]] = obelisk_sim.packed.unflatten
    // CHECK: %[[ASC_DOWN:.*]] = obelisk_sim.ref.extract %arg2 from 2
    // CHECK: obelisk_sim.ref.store %[[PACKED3]] to %[[ASC_DOWN]]
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %descending: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %ascending: !obelisk_sim.ref<!obelisk_sim.packed_array<0 : 7 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.descending", argument = 1,
                kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.ascending", argument = 2,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9500001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
          obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !descending_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 3 : i64, selection_kind = 1 : i32,
              semantic_type = !descending_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 4 : i64, referenced_path = "top.descending",
                referenced_symbol = @descending,
                semantic_type = !descending} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 5 : i64, constant_value = "4",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 6 : i64, constant_value = "3",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 7 : i64, constant_value = "3'b111",
              semantic_type = !descending_slice} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 9 : i64, assignment_kind = 0 : i32,
            semantic_type = !descending_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 10 : i64, selection_kind = 2 : i32,
              semantic_type = !descending_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 11 : i64, referenced_path = "top.descending",
                referenced_symbol = @descending,
                semantic_type = !descending} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 12 : i64, constant_value = "6",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 13 : i64, constant_value = "3",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 14 : i64, constant_value = "3'b111",
              semantic_type = !descending_slice} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 15 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 16 : i64, assignment_kind = 0 : i32,
            semantic_type = !ascending_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 17 : i64, selection_kind = 1 : i32,
              semantic_type = !ascending_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 18 : i64, referenced_path = "top.ascending",
                referenced_symbol = @ascending,
                semantic_type = !ascending} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 19 : i64, constant_value = "3",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 20 : i64, constant_value = "3",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 21 : i64, constant_value = "3'b111",
              semantic_type = !ascending_slice} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 22 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 23 : i64, assignment_kind = 0 : i32,
            semantic_type = !ascending_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 24 : i64, selection_kind = 2 : i32,
              semantic_type = !ascending_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 25 : i64, referenced_path = "top.ascending",
                referenced_symbol = @ascending,
                semantic_type = !ascending} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 26 : i64, constant_value = "5",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 27 : i64, constant_value = "3",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 28 : i64, constant_value = "3'b111",
              semantic_type = !ascending_slice} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
