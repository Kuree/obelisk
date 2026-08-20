// RUN: %split-file %s %t
// RUN: obelisk-opt %t/void-cast.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=VOID
// RUN: obelisk-opt %t/task.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=TASK

// IEEE 1800-2017 6.24.2: the task form of $cast reports a failed cast as a
// run-time error, while the function form reports failure through its 0
// result. 13.4.1 casts a call to the void type to use it as a statement, which
// keeps its function semantics, so a failed cast there stays silent.

// VOID-NOT: $cast failed when used as a task
// TASK: $cast failed when used as a task

//--- void-cast.mlir

// IEEE 1800-2017 6.24.2: the task form of $cast reports a failed cast as an
// error, while the function form reports failure through its 0 result.
// A.6.4 spells `void'(subroutine_call);` as its own statement form, in which
// the call keeps its function semantics, so only the bare `$cast(...)`
// statement below may raise the error.

// CHECK-COUNT-1: $cast failed when used as a task
// CHECK-NOT: $cast failed when used as a task

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "t.A", name = "A", node_id = 5 : i64, sym_name = "s5.A"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "t.B", name = "B", node_id = 6 : i64, sym_name = "s6.B"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "t.e_t", name = "e_t", node_id = 7 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s7.e_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.d", lifetime = 1 : i32, name = "d", node_id = 8 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s8.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.s", lifetime = 1 : i32, name = "s", node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.s"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$cast", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, dynamic_cast_enum_values = ["2'b1", "2'b10"], dynamic_cast_kind = 2 : i32, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, is_void_casted, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 15 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "t.d", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.d, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 17 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 18 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.s, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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

// IEEE 1800-2017 6.24.2: the task form of $cast reports a failed cast as an
// error, while the function form reports failure through its 0 result.
// A.6.4 spells `void'(subroutine_call);` as its own statement form, in which
// the call keeps its function semantics, so only the bare `$cast(...)`
// statement below may raise the error.

// CHECK-COUNT-1: $cast failed when used as a task
// CHECK-NOT: $cast failed when used as a task

module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu"} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "t", name = "t", node_id = 0 : i64, sym_name = "s0.t"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "t", is_uninstantiated = false, name = "t", node_id = 3 : i64, referenced_path = "t", referenced_symbol = @s0.t, sym_name = "s3.t"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "t", name = "t", node_id = 4 : i64, sym_name = "s4.t", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "t.A", name = "A", node_id = 5 : i64, sym_name = "s5.A"} {
        }
        obelisk.sv.symbol.transparent_member attributes {hierarchical_name = "t.B", name = "B", node_id = 6 : i64, sym_name = "s6.B"} {
        }
        obelisk.sv.type.type_alias attributes {hierarchical_name = "t.e_t", name = "e_t", node_id = 7 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s7.e_t"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.d", lifetime = 1 : i32, name = "d", node_id = 8 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>, sym_name = "s8.d"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "t.s", lifetime = 1 : i32, name = "s", node_id = 9 : i64, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>, sym_name = "s9.s"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "t", node_id = 10 : i64, procedure_kind = 0 : i32, sym_name = "s10", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 11 : i64} {
            obelisk.sv.statement.list attributes {node_id = 12 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 19 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "$cast", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, dynamic_cast_enum_values = ["2'b1", "2'b10"], dynamic_cast_kind = 2 : i32, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = true, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 20 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 1 : i32, system_library_cell = "work.t", system_scope_path = "t", system_scope_symbol = @s1.$root::@s3.t::@s4.t} {
                  obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 21 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 22 : i64, referenced_path = "t.d", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s8.d, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                    obelisk.sv.expression.empty_argument attributes {is_signed = false, node_id = 23 : i64, semantic_type = !obelisk.enum<"t.e_t", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "t.s", referenced_symbol = @s1.$root::@s3.t::@s4.t::@s9.s, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, true, 0 : 0, logic>>} {
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


