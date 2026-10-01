// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 21.2.1.2 permits integer format specifiers on enumerated
// values. Numeric formatting needs only the enum's packed value, so an enum
// declaration inventory is unnecessary here.
module {
  obelisk.sv.symbol.definition @top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 2 : i64} {}
    obelisk.sv.symbol.instance @instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @top} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", node_id = 5 : i64, procedure_kind = 0 : i32, time_precision_fs = 1 : i64, time_unit_fs = 1 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 6 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$write", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 7 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @root::@instance::@body} {
              obelisk.sv.expression.string_literal attributes {constant_value = "%x", folded_constant = "16'h2578", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {}
              obelisk.sv.expression.integer_literal attributes {constant_value = "2'b01", is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.enum<"anonymous", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {}
            }
          }
        }
      }
    }
  }
}

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[VALUE:.*]] = simulation.logic.constant 1 : i2
// CHECK-NOT: simulation.string.literal
// CHECK: simulation.display {{.*}}%[[VALUE]]{{.*}}flags = [0, 0]
