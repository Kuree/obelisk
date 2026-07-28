// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' | FileCheck %s

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!destination = !obelisk.ranged_unpacked_array<7 : 0 x !bit>
!replacement = !obelisk.ranged_unpacked_array<2 : 0 x !bit>
!simple_slice = !obelisk.ranged_unpacked_array<5 : 3 x !bit>
!indexed_slice = !obelisk.ranged_unpacked_array<4 : 6 x !bit>

module {
  obelisk_sim.design @slice_lvalue {
    obelisk_sim.code_unit.decl 9300001 in 0 initial
        hierarchy "test.slice_lvalue.9300001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 :
        !obelisk_sim.unpacked_array<7 : 0 x i1>
        design hierarchy "top.destination"
    obelisk_sim.storage.decl 1 in 0 :
        !obelisk_sim.unpacked_array<2 : 0 x i1>
        design hierarchy "top.replacement"

    // CHECK-LABEL: obelisk_sim.func @unit
    // CHECK-COUNT-3: obelisk_sim.ref.array_element %arg1
    // CHECK: obelisk_sim.aggregate.extract
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.aggregate.extract
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.aggregate.extract
    // CHECK: obelisk_sim.ref.store
    // CHECK-COUNT-3: obelisk_sim.ref.array_element %arg1
    // CHECK: obelisk_sim.aggregate.extract
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.aggregate.extract
    // CHECK: obelisk_sim.ref.store
    // CHECK: obelisk_sim.aggregate.extract
    // CHECK: obelisk_sim.ref.store
    obelisk_sim.func @unit(
        %ctx: !obelisk_sim.context
            {obelisk_sim.capture_kind = 0 : i32},
        %destination:
            !obelisk_sim.ref<!obelisk_sim.unpacked_array<7 : 0 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %replacement:
            !obelisk_sim.ref<!obelisk_sim.unpacked_array<2 : 0 x i1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {
          entry_kind = 1 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.destination",
                argument = 1, kind = direct, copyOut = false>,
            #obelisk_sim.argument_binding<path = "top.replacement",
                argument = 2, kind = direct, copyOut = false>
          ],
          code_unit_id = 9300001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !simple_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 3 : i64, selection_kind = 0 : i32,
              semantic_type = !simple_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 4 : i64, referenced_path = "top.destination",
                referenced_symbol = @destination,
                semantic_type = !destination} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 5 : i64, constant_value = "5",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 6 : i64, constant_value = "3",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 7 : i64, referenced_path = "top.replacement",
              referenced_symbol = @replacement,
              semantic_type = !replacement} {
          }
        }
      }
      obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 9 : i64, assignment_kind = 0 : i32,
            semantic_type = !indexed_slice} {
          obelisk.sv.expression.range_select attributes {
              node_id = 10 : i64, selection_kind = 1 : i32,
              semantic_type = !indexed_slice} {
            obelisk.sv.expression.named_value attributes {
                node_id = 11 : i64, referenced_path = "top.destination",
                referenced_symbol = @destination,
                semantic_type = !destination} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 12 : i64, constant_value = "4",
                semantic_type = !int} {
            }
            obelisk.sv.expression.integer_literal attributes {
                node_id = 13 : i64, constant_value = "3",
                semantic_type = !int} {
            }
          }
          obelisk.sv.expression.named_value attributes {
              node_id = 14 : i64, referenced_path = "top.replacement",
              referenced_symbol = @replacement,
              semantic_type = !replacement} {
          }
        }
      }
      obelisk_sim.return
    }
  }
}
