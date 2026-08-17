// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 23.2.2.4 permits a default value only on an input port, and
// 23.2.2.3 makes an output port declared with an explicit data type a
// variable. The initializer on such a port is therefore that variable's
// declaration assignment, and 10.5 requires setting a static variable's
// declared initial value "before any initial or always procedures are
// started".
//
// slang parents the initializer expression under the port symbol rather than
// the variable it initializes, so dropping it there left the port reading x
// for the whole simulation.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "port_initializer", name = "port_initializer", node_id = 0 : i64, sym_name = "s0.port_initializer"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "port_initializer", is_uninstantiated = false, name = "port_initializer", node_id = 3 : i64, referenced_path = "port_initializer", referenced_symbol = @s0.port_initializer, sym_name = "s3.port_initializer"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_initializer", name = "port_initializer", node_id = 4 : i64, sym_name = "s4.port_initializer", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.port attributes {direction = 1 : i32, hierarchical_name = "port_initializer.value", name = "value", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.value"} {
          obelisk.sv.expression.conversion attributes {folded_constant = "4'b1010", is_signed = false, node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "4'b1010", is_signed = false, node_id = 7 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "port_initializer.value", lifetime = 1 : i32, name = "value", node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.value"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "port_initializer", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s7", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.port_initializer", system_scope_path = "port_initializer", system_scope_symbol = @s1.$root::@s3.port_initializer::@s4.port_initializer} {
              obelisk.sv.expression.string_literal attributes {constant_value = "%h", is_signed = false, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 13 : i64, referenced_path = "port_initializer.value", referenced_symbol = @s1.$root::@s3.port_initializer::@s4.port_initializer::@s6.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              }
            }
          }
        }
      }
    }
  }
}


// CHECK: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 function hierarchy "port_initializer.value.$static_initializer" debug "value"

// The initializer runs as a zero-time call before the initial process spawns.
// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK:      obelisk_sim.call @unit_0(
// CHECK:      obelisk_sim.spawn @unit_1(

// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK:      %[[VALUE:.*]] = obelisk_sim.logic.constant -6 : i4, 0 : i4
// CHECK:      obelisk_sim.ref.store
