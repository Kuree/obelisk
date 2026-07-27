// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

// Drives indexed part-select lowering directly from hand-authored MLIR.  The
// second child is the select width, so a descending [4 +: 3] select starts at
// physical bit 4 rather than treating 3 as a second bound.

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!vector = !obelisk.ranged_packed_array<7 : 0 x !bit>
!slice = !obelisk.ranged_packed_array<6 : 4 x !bit>

module {
  obelisk_sim.design @indexed_part_select {
    obelisk_sim.code_unit.decl 9500001 in 0 initial
        hierarchy "top.indexed_part_select"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.packed_array<7 : 0 x i1>
        design hierarchy "top.value"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK: %[[ONES:.*]] = arith.constant -1 : i3
    // CHECK: %[[PACKED:.*]] = obelisk_sim.packed.unflatten %[[ONES]]
    // CHECK: %[[SLICE:.*]] = obelisk_sim.ref.extract %arg1 from 4
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x i1>>
    // CHECK-SAME: !obelisk_sim.ref<!obelisk_sim.packed_array<6 : 4 x i1>>
    // CHECK: obelisk_sim.ref.store %[[PACKED]] to %[[SLICE]]
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.packed_array<7 : 0 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.value", argument = 1,
                kind = direct, copyOut = false>
          ],
          code_unit_id = 9500001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 3 : i64, selection_kind = 1 : i32,
              semantic_type = !slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 4 : i64, referenced_path = "top.value",
                referenced_symbol = @value, semantic_type = !vector} {
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
              semantic_type = !slice} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
