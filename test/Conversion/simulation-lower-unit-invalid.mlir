// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// Exercise unit-lowering rejection directly on prepared semantic IR. This is
// pass coverage and intentionally does not involve the SystemVerilog driver.

!bit8 = !obelisk.integral<8, false, false, 7 : 0, bit>
!signed8 = !obelisk.integral<8, true, false, 7 : 0, bit>
!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  obelisk_sim.design @invalid_units {
    obelisk_sim.code_unit.decl 9000001 in 0 always_comb
        hierarchy "test.invalid_units.power.9000001"
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i8 design hierarchy "top.result"

    obelisk_sim.func @power(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %result: !obelisk_sim.ref<i8> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 4 : i32,
          obelisk_sim.bindings = [
            #obelisk_sim.argument_binding<path = "top.result", argument = 1,
                                          kind = direct, copyOut = false>
          ],
          code_unit_id = 9000001 : i64
        } {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.assignment attributes {
            node_id = 2 : i64, assignment_kind = 0 : i32,
            semantic_type = !bit8} {
          obelisk.sv.expression.named_value attributes {
              node_id = 3 : i64, referenced_path = "top.result",
              referenced_symbol = @result, semantic_type = !bit8} {
          }
          obelisk.sv.expression.conversion attributes {
              node_id = 4 : i64, semantic_type = !bit8} {
            obelisk.sv.expression.binary_op attributes {
                node_id = 5 : i64, operator_kind = 27 : i32,
                semantic_type = !logic8} {
              obelisk.sv.expression.named_value attributes {
                  node_id = 6 : i64, referenced_path = "top.result",
                  referenced_symbol = @result, semantic_type = !bit8} {
              }
              obelisk.sv.expression.named_value attributes {
                  node_id = 7 : i64, referenced_path = "top.result",
                  referenced_symbol = @result, semantic_type = !signed8} {
              }
            }
          }
        }
      }
      obelisk_sim.return
    }
  }
}

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: signed dynamic or negative integral power
