// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' -verify-diagnostics -o /dev/null

// Source:
//   checker chk(output logic o); endchecker
//   module m; logic x; chk u(x); endmodule
//
// A checker output argument behaves as a Reactive continuous assignment to its
// actual (IEEE 1800-2023 17.3); that connection is not lowered yet.

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.m attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
      talos.sv.symbol.checker @s3.chk attributes {hierarchical_name = "chk", name = "chk", node_id = 3 : i64, port_count = 1 : i64, port_paths = ["chk.o"], port_symbols = [@s1.$root::@s2::@s3.chk::@s4.o]} {
        talos.sv.symbol.assertion_port @s4.o attributes {direction = 1 : i32, has_default_value = false, hierarchical_name = "chk.o", is_local_variable = true, name = "o", node_id = 4 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
      }
    }
    talos.sv.symbol.instance @s5.m attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 5 : i64, referenced_path = "m", referenced_symbol = @s0.m} {
      talos.sv.symbol.instance_body @s6.m attributes {hierarchical_name = "m", name = "m", node_id = 6 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "m", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s7.x attributes {hierarchical_name = "m.x", lifetime = 1 : i32, name = "x", node_id = 7 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
        }
        talos.sv.symbol.checker_instance @s8.u attributes {connection_actual_kinds = array<i64: 0>, connection_attribute_counts = array<i64: 0>, connection_count = 1 : i64, connection_formal_paths = ["m.u.o"], connection_formal_symbols = [@s1.$root::@s5.m::@s6.m::@s8.u::@s10.chk::@s9.o], connection_has_actual = array<i64: 1>, connection_has_output_initial = array<i64: 0>, hierarchical_name = "m.u", is_procedural = false, name = "u", node_id = 8 : i64, referenced_checker_path = "chk", referenced_checker_symbol = @s1.$root::@s2::@s3.chk} {
          talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 9 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            talos.sv.expression.named_value attributes {is_signed = false, node_id = 10 : i64, referenced_path = "m.x", referenced_symbol = @s1.$root::@s5.m::@s6.m::@s7.x, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            }
            talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 11 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            }
          }
          talos.sv.symbol.checker_instance_body @s10.chk attributes {hierarchical_name = "m.u", instance_depth = 0 : i64, instance_flags = 0 : i64, is_procedural = false, name = "chk", node_id = 12 : i64, parent_instance_path = "m.u", parent_instance_symbol = @s1.$root::@s5.m::@s6.m::@s8.u, referenced_checker_path = "chk", referenced_checker_symbol = @s1.$root::@s2::@s3.chk} {
            // expected-error @below {{IEEE 1800-2023 17.3 checker output arguments are not executable yet}}
            talos.sv.symbol.formal_argument @s9.o attributes {direction = 1 : i32, hierarchical_name = "m.u.o", lifetime = 1 : i32, name = "o", node_id = 13 : i64, semantic_type = !talos.integral<1, false, true, 0 : 0, logic>} {
            }
          }
        }
      }
    }
  }
}

