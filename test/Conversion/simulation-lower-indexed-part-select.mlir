// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

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
!pair = !obelisk.ranged_packed_array<1 : 0 x !bit>
!nested = !obelisk.ranged_packed_array<2 : 0 x !pair>
!nested_slice = !obelisk.ranged_packed_array<2 : 1 x !pair>

module {
  simulation.design @indexed_part_select {
    simulation.code_unit.decl 9500001 in 0 initial
        hierarchy "top.indexed_part_select"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 :
        !simulation.packed_array<7 : 0 x i1>
        design hierarchy "top.descending"
    simulation.storage.decl 1 in 0 :
        !simulation.packed_array<0 : 7 x i1>
        design hierarchy "top.ascending"
    simulation.storage.decl 2 in 0 :
        !simulation.packed_array<2 : 0 x !simulation.packed_array<1 : 0 x i1>>
        design hierarchy "top.nested"
    simulation.storage.decl 3 in 0 : i32 design hierarchy "top.index"

    // CHECK-LABEL: simulation.func @unit
    // CHECK: %[[ONES:.*]] = arith.constant -1 : i3
    // CHECK: %[[PACKED0:.*]] = simulation.packed.unflatten %[[ONES]]
    // CHECK: %[[DESC_UP:.*]] = simulation.ref.extract %arg1 from 4
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<7 : 0 x i1>>
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<6 : 4 x i1>>
    // CHECK: simulation.ref.store %[[PACKED0]] to %[[DESC_UP]]
    // CHECK: %[[PACKED1:.*]] = simulation.packed.unflatten
    // CHECK: %[[DESC_DOWN:.*]] = simulation.ref.extract %arg1 from 4
    // CHECK: simulation.ref.store %[[PACKED1]] to %[[DESC_DOWN]]
    // CHECK: %[[PACKED2:.*]] = simulation.packed.unflatten
    // CHECK: %[[ASC_UP:.*]] = simulation.ref.extract %arg2 from 2
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<0 : 7 x i1>>
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<3 : 5 x i1>>
    // CHECK: simulation.ref.store %[[PACKED2]] to %[[ASC_UP]]
    // CHECK: %[[PACKED3:.*]] = simulation.packed.unflatten
    // CHECK: %[[ASC_DOWN:.*]] = simulation.ref.extract %arg2 from 2
    // CHECK: simulation.ref.store %[[PACKED3]] to %[[ASC_DOWN]]
    // CHECK: %[[PACKED4:.*]] = simulation.packed.unflatten
    // CHECK: %[[NESTED_DOWN:.*]] = simulation.ref.extract %arg3 from 2
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<2 : 0 x !simulation.packed_array<1 : 0 x i1>>>
    // CHECK-SAME: !simulation.ref<!simulation.packed_array<2 : 1 x !simulation.packed_array<1 : 0 x i1>>>
    // CHECK: simulation.ref.store %[[PACKED4]] to %[[NESTED_DOWN]]
    // CHECK: %[[INDEX:.*]] = simulation.ref.load %arg4
    // CHECK: %[[WIDE_INDEX:.*]] = arith.extsi %[[INDEX]] : i32 to i67
    // CHECK: %[[SCALE:.*]] = arith.constant 2 : i67
    // CHECK: %[[SCALED:.*]] = arith.muli %[[WIDE_INDEX]], %[[SCALE]]
    // CHECK: %[[ADJUSTMENT:.*]] = arith.constant 2 : i67
    // CHECK: %[[LOW:.*]] = arith.subi %[[SCALED]], %[[ADJUSTMENT]]
    // CHECK: simulation.bits.dyn_extract {{.*}} from %[[LOW]]
    simulation.func @unit(
        %ctx: !simulation.context
            {simulation.capture_kind = 0 : i32},
        %descending: !simulation.ref<!simulation.packed_array<7 : 0 x i1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %ascending: !simulation.ref<!simulation.packed_array<0 : 7 x i1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %nested: !simulation.ref<!simulation.packed_array<2 : 0 x !simulation.packed_array<1 : 0 x i1>>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %index: !simulation.ref<i32>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {
          entry_kind = 1 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.descending", argument = 1,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.ascending", argument = 2,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.nested", argument = 3,
                kind = direct, copyOut = false>,
            #simulation.argument_binding<path = "top.index", argument = 4,
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
      obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 30 : i64, assignment_kind = 0 : i32,
            semantic_type = !nested_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 31 : i64, selection_kind = 2 : i32,
              semantic_type = !nested_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 32 : i64, referenced_path = "top.nested",
                referenced_symbol = @nested,
                semantic_type = !nested} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 33 : i64, constant_value = "2",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 34 : i64, constant_value = "2",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.integer_literal attributes {
              node_id = 35 : i64, constant_value = "4'b1111",
              semantic_type = !nested_slice} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 36 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 37 : i64, assignment_kind = 0 : i32,
            semantic_type = !nested_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 38 : i64, selection_kind = 2 : i32,
              semantic_type = !nested_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 39 : i64, referenced_path = "top.nested",
                referenced_symbol = @nested,
                semantic_type = !nested} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 40 : i64, constant_value = "2",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 41 : i64, constant_value = "2",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.range_select attributes {
              node_id = 42 : i64, selection_kind = 2 : i32,
              semantic_type = !nested_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 43 : i64, referenced_path = "top.nested",
                referenced_symbol = @nested,
                semantic_type = !nested} {
            }
            obelisk.sv.expression.named_value attributes {
                node_id = 44 : i64, referenced_path = "top.index",
                referenced_symbol = @index,
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 45 : i64, constant_value = "2",
                semantic_type = !int} {
            }
          }
        }
      }
      simulation.return
    }
  }
}
