// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 4.9.1: a continuous assignment process "is also evaluated at
// time zero in order to propagate constant values. This includes implicit
// continuous assignments inferred from port connections."
//
// The reader module is declared before the module that instantiates it, so its
// initial process is prepared first. A constant port connection never
// transitions after time zero, so spawning it after that reader leaves the
// driven net at its default value for the whole simulation. The root
// initializer must therefore spawn the port-connection unit first.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "port_time_zero_reader", name = "port_time_zero_reader", node_id = 0 : i64, sym_name = "s0.port_time_zero_reader"} {
  }
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "port_time_zero_top", name = "port_time_zero_top", node_id = 1 : i64, sym_name = "s1.port_time_zero_top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 2 : i64, sym_name = "s2.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 3 : i64, sym_name = "s3"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "port_time_zero_top", is_uninstantiated = false, name = "port_time_zero_top", node_id = 4 : i64, referenced_path = "port_time_zero_top", referenced_symbol = @s1.port_time_zero_top, sym_name = "s4.port_time_zero_top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_time_zero_top", name = "port_time_zero_top", node_id = 5 : i64, sym_name = "s5.port_time_zero_top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.instance attributes {hierarchical_name = "port_time_zero_top.reader", is_uninstantiated = false, name = "reader", node_id = 6 : i64, referenced_path = "port_time_zero_reader", referenced_symbol = @s0.port_time_zero_reader, sym_name = "s6.reader"} {
          obelisk.sv.port.connection attributes {actual_is_constant = true, direction = 0 : i32, formal_name = "value", formal_ordinal = 0 : i64, formal_path = "port_time_zero_top.reader.value", formal_symbol = @s2.$root::@s4.port_time_zero_top::@s5.port_time_zero_top::@s6.reader::@s7.port_time_zero_reader::@s8.value, formal_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, internal_path = "port_time_zero_top.reader.value", internal_symbol = @s2.$root::@s4.port_time_zero_top::@s5.port_time_zero_top::@s6.reader::@s7.port_time_zero_reader::@s9.value, is_ansi = true, is_net = true, node_id = 7 : i64, provenance = 0 : i32} {
          } {
            obelisk.sv.expression.conversion attributes {folded_constant = "4'b1111", is_signed = false, node_id = 8 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "4'b1111", is_signed = false, node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
            }
          }
          obelisk.sv.symbol.instance_body attributes {hierarchical_name = "port_time_zero_top.reader", name = "port_time_zero_reader", node_id = 10 : i64, sym_name = "s7.port_time_zero_reader", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.symbol.port attributes {direction = 0 : i32, hierarchical_name = "port_time_zero_top.reader.value", name = "value", node_id = 11 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.value"} {
            }
            obelisk.sv.symbol.net attributes {hierarchical_name = "port_time_zero_top.reader.value", is_implicit = false, name = "value", net_kind = 1 : i32, node_id = 12 : i64, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.value"} {
            }
            obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "port_time_zero_top.reader", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$display", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.void, subroutine_kind = 1 : i32, system_library_cell = "work.port_time_zero_reader", system_scope_path = "port_time_zero_top.reader", system_scope_symbol = @s2.$root::@s4.port_time_zero_top::@s5.port_time_zero_top::@s6.reader::@s7.port_time_zero_reader} {
                  obelisk.sv.expression.string_literal attributes {constant_value = "%b", is_signed = false, node_id = 16 : i64, semantic_type = !obelisk.ranged_packed_array<15 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 17 : i64, referenced_path = "port_time_zero_top.reader.value", referenced_symbol = @s2.$root::@s4.port_time_zero_top::@s5.port_time_zero_top::@s6.reader::@s7.port_time_zero_reader::@s9.value, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                  }
                }
              }
            }
          }
        }
      }
    }
  }
}


// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in 2 initial hierarchy "port_time_zero_top.reader.$code_unit_13"
// CHECK-DAG: obelisk_sim.code_unit.decl {{[0-9]+}} in 1 port_initialize hierarchy "port_time_zero_top.reader.$port_connection_0"

// The port-connection initializer drives, so it takes the driver capture; the
// initial process only reads the net.
// CHECK-LABEL: obelisk_sim.func @__obelisk_root
// CHECK:      obelisk_sim.spawn @unit_1(
// CHECK-SAME:   !obelisk_sim.driver<
// CHECK-NEXT: obelisk_sim.spawn @unit_0(
// CHECK-SAME:   !obelisk_sim.net<
// CHECK-NEXT: obelisk_sim.return
