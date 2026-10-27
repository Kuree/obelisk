// RUN: talos-opt %s '--lower-talos-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2023 21.3.3: a format string or an $sformat/$swrite destination
// may be a dynamic array or queue of byte. The format string is the array's
// text in index order, which the runtime renders as %s does. A destination
// takes the size of the text (5.9), and a bounded queue keeps its first
// bound + 1 characters (7.10.5).
//
//   module top;
//     byte fmt[]; byte da[]; byte q[$:1]; string s;
//     initial begin
//       s = $sformatf(fmt, 7);
//       $sformat(da, "%0d", 42);
//       $swrite(q, "ab");
//     end
//   endmodule

// CHECK-LABEL: simulation.func private @unit_0(
// CHECK: %[[TEXT_FORMAT:.*]] = simulation.bytes.constant "%s"
// CHECK: %[[FMT:.*]] = simulation.ref.load %arg1
// CHECK: %[[FORMAT:.*]] = simulation.string.output_format %arg0(%[[TEXT_FORMAT]], %[[FMT]]) radix = <decimal> flags = [32, 16] :
// CHECK: simulation.string.output_format %arg0(%[[FORMAT]],
// CHECK: %[[DTEXT:.*]] = simulation.string.output_format
// CHECK: %[[DLEN:.*]] = simulation.string.length %[[DTEXT]]
// CHECK: %[[DA:.*]] = simulation.container.create %[[DLEN]] {{.*}}container_kind = #simulation.container_kind<dynamic_array>
// CHECK: %[[DC:.*]] = simulation.string.getc %[[DTEXT]], %{{.*}}
// CHECK: simulation.container.write %[[DA]], %{{.*}}, %[[DC]]
// CHECK: simulation.ref.store %{{.*}} to %arg2
// CHECK: %[[QTEXT:.*]] = simulation.string.output_format
// CHECK: %[[QLEN:.*]] = simulation.string.length %[[QTEXT]]
// CHECK: %[[QCOUNT:.*]] = arith.minui %[[QLEN]], %{{.*}}
// CHECK: %[[Q:.*]] = simulation.container.create %{{.*}} {{.*}}bound = 1 : i64, container_kind = #simulation.container_kind<queue>
// CHECK: arith.cmpi ult, %{{.*}}, %[[QCOUNT]]
// CHECK: %[[QC:.*]] = simulation.string.getc %[[QTEXT]], %{{.*}}
// CHECK: simulation.queue.insert %[[QC]] into %[[Q]][
// CHECK: simulation.ref.store %{{.*}} to %arg3

module attributes {coverage.language_version = 2023 : i32} {
  talos.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64, simulation.vpi_definition_name = "$unit"} {
    }
    talos.sv.symbol.instance @s3.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 3 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      talos.sv.symbol.instance_body @s4.top attributes {hierarchical_name = "top", name = "top", node_id = 4 : i64, simulation.vpi_automatic = false, simulation.vpi_cell_instance = false, simulation.vpi_definition_name = "top", simulation.vpi_top = true, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, vpi_scope_kind = 32 : i32} {
        talos.sv.symbol.variable @s5.fmt attributes {hierarchical_name = "top.fmt", lifetime = 1 : i32, name = "fmt", node_id = 5 : i64, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
        }
        talos.sv.symbol.variable @s6.da attributes {hierarchical_name = "top.da", lifetime = 1 : i32, name = "da", node_id = 6 : i64, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
        }
        talos.sv.symbol.variable @s7.q attributes {hierarchical_name = "top.q", lifetime = 1 : i32, name = "q", node_id = 7 : i64, semantic_type = !talos.queue<!talos.integral<8, true, false, 7 : 0, byte>, 1>} {
        }
        talos.sv.symbol.variable @s8.s attributes {hierarchical_name = "top.s", lifetime = 1 : i32, name = "s", node_id = 8 : i64, semantic_type = !talos.string} {
        }
        talos.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 10 : i64} {
            talos.sv.statement.list attributes {node_id = 11 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 12 : i64} {
                talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 13 : i64, semantic_type = !talos.string} {
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 14 : i64, referenced_path = "top.s", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s8.s, semantic_type = !talos.string} {
                  }
                  talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$sformatf", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !talos.string, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.fmt", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s5.fmt, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
                    }
                    talos.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 18 : i64} {
                talos.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "$sformat", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 20 : i64, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "top.da", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s6.da, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
                    }
                    talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 22 : i64, semantic_type = !talos.dynarray<!talos.integral<8, true, false, 7 : 0, byte>>} {
                    }
                  }
                  talos.sv.expression.string_literal attributes {constant_value = "%0d", is_signed = false, node_id = 23 : i64, semantic_type = !talos.ranged_packed_array<23 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "42", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              talos.sv.statement.expression_statement attributes {node_id = 25 : i64} {
                talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$swrite", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = true, node_id = 26 : i64, semantic_type = !talos.void, subroutine_kind = 1 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s3.top::@s4.top} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 27 : i64, semantic_type = !talos.queue<!talos.integral<8, true, false, 7 : 0, byte>, 1>} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "top.q", referenced_symbol = @s1.$root::@s3.top::@s4.top::@s7.q, semantic_type = !talos.queue<!talos.integral<8, true, false, 7 : 0, byte>, 1>} {
                    }
                    talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 29 : i64, semantic_type = !talos.queue<!talos.integral<8, true, false, 7 : 0, byte>, 1>} {
                    }
                  }
                  talos.sv.expression.string_literal attributes {constant_value = "ab", is_signed = false, node_id = 30 : i64, semantic_type = !talos.ranged_packed_array<15 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>} {
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

