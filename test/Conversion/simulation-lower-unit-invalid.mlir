// RUN: not obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' 2>&1 | FileCheck %s

// Exercise unit-lowering rejection directly on prepared semantic IR. This is
// pass coverage and intentionally does not involve the SystemVerilog driver.

!bit8 = !obelisk.integral<8, false, false, 7 : 0, bit>
!logic8 = !obelisk.integral<8, false, true, 7 : 0, logic>

module {
  simulation.design @invalid_units {
    simulation.code_unit.decl 9000001 in 0 always_comb
        hierarchy "test.invalid_units.type_reference.9000001"
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i8 design hierarchy "top.result"

    simulation.func @type_reference(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %result: !simulation.ref<i8> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {
          entry_kind = 4 : i32,
          simulation.bindings = [
            #simulation.argument_binding<path = "top.result", argument = 1,
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
            obelisk.sv.expression.type_reference attributes {
                node_id = 5 : i64, semantic_type = !logic8} {
            }
          }
        }
      }
      simulation.return
    }
  }
}

// CHECK: unsupported semantic node in the first simulation slice
// CHECK-SAME: obelisk.sv.expression.type_reference
