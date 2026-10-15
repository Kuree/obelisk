// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 21.3.4.4: $fread ignores start and count when it loads an
// integral variable, which reads one packed value.
//
//   module top;
//     int fd, n; byte value;
//     initial n = $fread(value, fd, 1, 1);
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK-NOT: cf.cond_br
// CHECK: %[[DATA:.*]], %[[COUNT:.*]] = simulation.file.read_packed %arg0, %{{.*}} : (!simulation.context, i32) -> (i8, i32)
// CHECK-NOT: cf.cond_br
// CHECK: simulation.ref.store %{{.*}} to %arg3 : i8
// CHECK: simulation.ref.store %[[COUNT]] to %arg2 : i32
// CHECK-NEXT: simulation.return

module attributes {coverage.language_version = 2023 : i32} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    obelisk.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        obelisk.sv.symbol.variable @s5.fd attributes {hierarchical_name = "top.fd", lifetime = 1 : i32, name = "fd", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s6.n attributes {hierarchical_name = "top.n", lifetime = 1 : i32, name = "n", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s7.value attributes {hierarchical_name = "top.value", lifetime = 1 : i32, name = "value", node_id = 7 : i64, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
        }
        obelisk.sv.symbol.procedural_block @s8 attributes {hierarchical_name = "top", node_id = 8 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "top.n", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.n, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.conversion attributes {is_implicit = true, is_signed = true, node_id = 12 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "$fread", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 14 : i64, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "top.value", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.value, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<8, true, false, 7 : 0, byte>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 17 : i64, referenced_path = "top.fd", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fd, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

