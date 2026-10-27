// RUN: %split-file %s %t
// RUN: talos-opt %t/void-cast.mlir '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=VOID
// RUN: talos-opt %t/task.mlir '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=TASK

// IEEE 1800-2017 6.24.2: the task form of $cast reports a failed cast as a
// run-time error, while the function form reports failure through its 0
// result. 13.4.1 casts a call to the void type to use it as a statement, which
// keeps its function semantics, so a failed cast there stays silent.

// VOID-NOT: $cast failed when used as a task
// VOID-NOT: calling nonvoid function
// TASK: warning: calling nonvoid function '$cast' as a statement discards its return value
// TASK: $cast failed when used as a task

//--- void-cast.mlir

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.t attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.t attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t} {
      talos.sv.symbol.instance_body @s4.t attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.transparent_member @s5.A attributes {hierarchical_name = "t.A", name = "A", node_id = 5 : i64} {
        }
        talos.sv.symbol.transparent_member @s6.B attributes {hierarchical_name = "t.B", name = "B", node_id = 6 : i64} {
        }
        talos.sv.type.type_alias @s7.e_t attributes {hierarchical_name = "t.e_t", name = "e_t", node_id = 7 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.variable @s8.d attributes {hierarchical_name = "t.d", lifetime = 1 : i32, name = "d", node_id = 8 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.variable @s9.s attributes {hierarchical_name = "t.s", lifetime = 1 : i32, name = "s", node_id = 9 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "t", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 11 : i64} {
            talos.sv.statement.list attributes {node_id = 12 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$cast", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, dynamic_cast_enum_values = ["2'b1", "2'b10"], dynamic_cast_kind = 2 : i32, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 14 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "t.d", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.d, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 17 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.s, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
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

//--- task.mlir

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  talos.sv.symbol.definition @s0.t attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64} {
  }
  talos.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    talos.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    talos.sv.symbol.instance @s3.t attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t} {
      talos.sv.symbol.instance_body @s4.t attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        talos.sv.symbol.transparent_member @s5.A attributes {hierarchical_name = "t.A", name = "A", node_id = 5 : i64} {
        }
        talos.sv.symbol.transparent_member @s6.B attributes {hierarchical_name = "t.B", name = "B", node_id = 6 : i64} {
        }
        talos.sv.type.type_alias @s7.e_t attributes {hierarchical_name = "t.e_t", name = "e_t", node_id = 7 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.variable @s8.d attributes {hierarchical_name = "t.d", lifetime = 1 : i32, name = "d", node_id = 8 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
        }
        talos.sv.symbol.variable @s9.s attributes {hierarchical_name = "t.s", lifetime = 1 : i32, name = "s", node_id = 9 : i64, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
        }
        talos.sv.symbol.procedural_block @s10 attributes {hierarchical_name = "t", node_id = 10 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          talos.sv.statement.block attributes {node_id = 11 : i64} {
            talos.sv.statement.list attributes {node_id = 12 : i64} {
              talos.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                talos.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$cast", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, dynamic_cast_enum_values = ["2'b1", "2'b10"], dynamic_cast_kind = 2 : i32, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                  talos.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                    talos.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "t.d", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.d, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    talos.sv.expression.empty_argument attributes {is_signed = false, node_id = 23 : i64, semantic_type = !talos.enum<"t.e_t", !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                  }
                  talos.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.s, semantic_type = !talos.ranged_packed_array<1 : 0 x !talos.integral<1, false, true, 0 : 0, logic>>} {
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

