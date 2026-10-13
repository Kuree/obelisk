// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s --split-input-file '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

// Random enum, tagged-union, and dist domains lower to finite solver
// programs that both execution tiers accept. Domains that need Z3 are in
// obelisk-to-simulation-random-domains-z3.mlir.

// CHECK: simulation.class.field
// CHECK-SAME: : i4
// CHECK-SAME: debug_name = "e"
// CHECK: simulation.random.cycle_next
// CHECK-SAME: width = 2 : i32
// CHECK: arith.cmpi ult
// CHECK-DAG: arith.constant {{.*}} 1 : i64
// CHECK-DAG: arith.constant {{.*}} 4 : i64
// CHECK-DAG: arith.constant {{.*}} 9 : i64
// CHECK: simulation.random.solve

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property @s4.e attributes {hierarchical_name = "C::e", name = "e", node_id = 4 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
        obelisk.sv.symbol.variable @s5.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
      }
      obelisk.sv.type.enum_type @s6.E attributes {hierarchical_name = "E", name = "E", node_id = 6 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
        obelisk.sv.symbol.enum_value @s7.A attributes {constant_value = "4'b1", hierarchical_name = "E.A", name = "A", node_id = 7 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
        obelisk.sv.symbol.enum_value @s8.B attributes {constant_value = "4'b100", hierarchical_name = "E.B", name = "B", node_id = 8 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
        obelisk.sv.symbol.enum_value @s9.C attributes {constant_value = "4'b1001", hierarchical_name = "E.C", name = "C", node_id = 9 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
      }
    }
    obelisk.sv.symbol.instance @s10.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s11.top attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s12.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
        obelisk.sv.symbol.procedural_block @s13 attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s10.top::@s11.top} {
              obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s10.top::@s11.top::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
            }
          }
        }
      }
    }
  }
}

// -----

// CHECK-LABEL: // {{-----}}
// CHECK: simulation.class.field
// CHECK-SAME: !simulation.packed_union
// CHECK-DAG: arith.constant {{.*}} 21 : i64
// CHECK-DAG: arith.constant {{.*}} 60 : i64
// CHECK-DAG: arith.constant {{.*}} 48 : i64
// CHECK-DAG: arith.constant {{.*}} 16 : i64
// CHECK-DAG: arith.constant {{.*}} 63 : i64
// CHECK-DAG: arith.constant {{.*}} 32 : i64
// CHECK-DAG: simulation.random.solve {{.*}} limit
// CHECK-DAG: simulation.packed.unflatten

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.T attributes {bitstream_width = 6 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "T", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "T", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>, this_variable_path = "T::this", this_variable_symbol = @s1.$root::@s2::@s3.T::@s5.this} {
        obelisk.sv.symbol.class_property @s4.u attributes {hierarchical_name = "T::u", name = "u", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.source_aggregate<"U", true, true, true, false, false, false, 6, 6, 6, 2, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "none", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.void}]>} {}
        obelisk.sv.symbol.variable @s5.this attributes {hierarchical_name = "T::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>} {}
      }
    }
    obelisk.sv.symbol.instance @s6.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 6 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s7.top attributes {hierarchical_name = "top", name = "top", node_id = 7 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s8.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 8 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>} {}
        obelisk.sv.symbol.procedural_block @s9 attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s6.top::@s7.top} {
              obelisk.sv.expression.named_value attributes {node_id = 12 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s6.top::@s7.top::@s8.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>} {}
            }
          }
        }
      }
    }
  }
}

// -----

// CHECK-LABEL: // {{-----}}
// CHECK: arith.constant {{.*}} 6 : i64
// CHECK: arith.constant {{.*}} 3 : i64
// CHECK-NOT: simulation.random.bounded
// CHECK: simulation.random.solve
// CHECK-SAME: limit

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {}
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.C attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property @s4.e attributes {hierarchical_name = "C::e", name = "e", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
        obelisk.sv.symbol.constraint_block @s17.weighted attributes {hierarchical_name = "C::weighted", name = "weighted", node_id = 17 : i64, this_variable_path = "C::weighted.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s17.weighted::@s29.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 18 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 19 : i64} {
              obelisk.sv.expression.dist attributes {default_weight_kind = 0 : i64, has_default_weight = false, item_count = 2 : i64, item_has_weight = array<i64: 1, 1>, item_weight_kinds = array<i64: 1, 0>, node_id = 20 : i64, semantic_type = !obelisk.void} {
                obelisk.sv.expression.conversion attributes {node_id = 21 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "C::e", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.e, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
                }
                obelisk.sv.expression.value_range attributes {node_id = 23 : i64, range_kind = 0 : i32, semantic_type = !obelisk.void} {
                  obelisk.sv.expression.conversion attributes {node_id = 24 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                  }
                  obelisk.sv.expression.conversion attributes {node_id = 26 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                    obelisk.sv.expression.integer_literal attributes {constant_value = "4", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "6", node_id = 28 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                obelisk.sv.expression.conversion attributes {node_id = 30 : i64, semantic_type = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "9", node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "3", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {}
              }
            }
          }
          obelisk.sv.symbol.variable @s29.this attributes {hierarchical_name = "C::weighted.this", is_compiler_generated, is_const, name = "this", node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
        }
        obelisk.sv.symbol.variable @s5.this attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
      }
      obelisk.sv.type.enum_type @s6.E attributes {hierarchical_name = "E", name = "E", node_id = 6 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
        obelisk.sv.symbol.enum_value @s7.A attributes {constant_value = "4'b1", hierarchical_name = "E.A", name = "A", node_id = 7 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
        obelisk.sv.symbol.enum_value @s8.B attributes {constant_value = "4'b100", hierarchical_name = "E.B", name = "B", node_id = 8 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
        obelisk.sv.symbol.enum_value @s9.C attributes {constant_value = "4'b1001", hierarchical_name = "E.C", name = "C", node_id = 9 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
      }
    }
    obelisk.sv.symbol.instance @s10.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s11.top attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s12.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
        obelisk.sv.symbol.procedural_block @s13 attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s10.top::@s11.top} {
              obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s10.top::@s11.top::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {}
            }
          }
        }
      }
    }
  }
}

// -----

// An unused random enum needs no solver program.
// CHECK-LABEL: // {{-----}}

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root @s0.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.compilation_unit @s1 attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      obelisk.sv.type.enum_type @s2.Unused attributes {hierarchical_name = "Unused", name = "Unused", node_id = 2 : i64, semantic_type = !obelisk.enum<"Unused", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
        obelisk.sv.symbol.enum_value @s3.X attributes {constant_value = "4'bx", hierarchical_name = "Unused.X", name = "X", node_id = 3 : i64, semantic_type = !obelisk.enum<"Unused", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
      }
    }
  }
}
