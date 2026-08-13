// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "function_local_sensitivity", name = "function_local_sensitivity", node_id = 0 : i64, sym_name = "s0.function_local_sensitivity"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "function_local_sensitivity", is_uninstantiated = false, name = "function_local_sensitivity", node_id = 3 : i64, referenced_path = "function_local_sensitivity", referenced_symbol = @s0.function_local_sensitivity, sym_name = "s3.function_local_sensitivity"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "function_local_sensitivity", name = "function_local_sensitivity", node_id = 4 : i64, sym_name = "s4.function_local_sensitivity", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "function_local_sensitivity.source", lifetime = 1 : i32, name = "source", node_id = 5 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s5.source"} {
        }
        obelisk.sv.symbol.net attributes {hierarchical_name = "function_local_sensitivity.destination", is_implicit = false, name = "destination", net_kind = 1 : i32, node_id = 6 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s6.destination"} {
          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "transform", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 7 : i64, referenced_path = "function_local_sensitivity.transform", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, subroutine_kind = 0 : i32} {
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 8 : i64, referenced_path = "function_local_sensitivity.source", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s5.source, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
            }
          }
        }
        obelisk.sv.symbol.subroutine attributes {default_lifetime = 1 : i32, hierarchical_name = "function_local_sensitivity.transform", name = "transform", node_id = 9 : i64, return_variable_path = "function_local_sensitivity.transform.transform", return_variable_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s9.transform, semantic_type = !obelisk.subroutine<(!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>) -> !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, false>, subroutine_kind = 0 : i32, sym_name = "s7.transform", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 11 : i64, referenced_path = "function_local_sensitivity.transform.scratch", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s10.scratch} {
            }
            obelisk.sv.statement.variable_declaration attributes {node_id = 28 : i64, referenced_path = "function_local_sensitivity.transform.read_only", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s11.read_only} {
            }
            obelisk.sv.statement.block attributes {node_id = 12 : i64} {
              obelisk.sv.statement.list attributes {node_id = 13 : i64} {
                obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "function_local_sensitivity.transform.scratch", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s10.scratch, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_signed = true, node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                      obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 18 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 19 : i64, referenced_path = "function_local_sensitivity.transform.value", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s8.value, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                        }
                      }
                    }
                  }
                }
                obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "function_local_sensitivity.transform.transform", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s9.transform, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                    }
                    obelisk.sv.expression.conversion attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
                      obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 29 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 24 : i64, referenced_path = "function_local_sensitivity.transform.scratch", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s10.scratch, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                        obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 30 : i64, referenced_path = "function_local_sensitivity.transform.read_only", referenced_symbol = @s1.$root::@s3.function_local_sensitivity::@s4.function_local_sensitivity::@s7.transform::@s11.read_only, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>} {
                        }
                      }
                    }
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "function_local_sensitivity.transform.value", lifetime = 1 : i32, name = "value", node_id = 25 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s8.value"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "function_local_sensitivity.transform.transform", is_compiler_generated, name = "transform", node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.transform"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "function_local_sensitivity.transform.scratch", lifetime = 1 : i32, name = "scratch", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s10.scratch"} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "function_local_sensitivity.transform.read_only", lifetime = 1 : i32, name = "read_only", node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, true, 31 : 0, integer>, sym_name = "s11.read_only"} {
          }
          // Compile-time declarations may be direct subroutine children. They
          // stay in this symbol table rather than being cloned into sim.func.
          obelisk.sv.symbol.iterator attributes {array_type = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, hierarchical_name = "function_local_sensitivity.transform.i", index_method_name = "", is_const, name = "i", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.i"} {
          }
          obelisk.sv.type.type_alias attributes {hierarchical_name = "function_local_sensitivity.transform.local_t", name = "local_t", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s13.local_t"} {
          }
        }
      }
    }
  }
}

// The continuous process still captures and passes both function-local
// statics. Its implicit sensitivity excludes the callee-written scratch but
// retains the true design input and the read-only static.
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} {{.*}} hierarchy "function_local_sensitivity.source"
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} {{.*}} hierarchy "function_local_sensitivity.transform.scratch"
// CHECK: obelisk_sim.storage.decl {{[0-9]+}} {{.*}} hierarchy "function_local_sensitivity.transform.read_only"
// CHECK-LABEL: obelisk_sim.func private @{{.*}}(
// CHECK-SAME: %{{.*}}, %[[SOURCE_ARG:arg[0-9]+]]: {{.*}}, %[[SCRATCH_ARG:arg[0-9]+]]: {{.*}}, %[[READ_ONLY_ARG:arg[0-9]+]]: {{.*}}, %{{.*}}) attributes {{.*}}entry_kind = 7 : i32
// CHECK: obelisk_sim.call {{.*}}%[[SCRATCH_ARG]], %[[READ_ONLY_ARG]]
// CHECK: obelisk_sim.suspend.any %[[SOURCE_ARG]], %[[READ_ONLY_ARG]]
// CHECK-NOT: obelisk.sv.
