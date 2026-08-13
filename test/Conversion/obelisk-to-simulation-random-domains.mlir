// RUN: %split-file %s %t
// RUN: obelisk-opt %t/enum.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=ENUM
// RUN: obelisk-opt %t/tagged.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=TAGGED
// RUN: obelisk-opt %t/dist.mlir '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s --check-prefix=DIST
// RUN: obelisk-opt %t/enum.mlir '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t/enum.mlir '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: obelisk-opt %t/tagged.mlir '--lower-obelisk-to-sim=opt-level=0' --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %t/tagged.mlir '--lower-obelisk-to-sim=opt-level=0' '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-enum.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     | FileCheck %s --check-prefix=WIDE-ENUM \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-enum.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null \
// RUN: %}
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/wide-enum.mlir '--lower-obelisk-to-sim=opt-level=0' \
// RUN:     '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null \
// RUN: %}
// RUN: obelisk-opt %t/unused-enum.mlir '--lower-obelisk-to-sim=opt-level=0' -o /dev/null
// RUN: %if z3 %{ \
// RUN:   obelisk-opt %t/unsat.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 \
// RUN:     | FileCheck %s --check-prefix=UNSAT \
// RUN: %}

// ENUM: obelisk_sim.class.field
// ENUM-SAME: : i4
// ENUM-SAME: debug_name = "e"
// ENUM: obelisk_sim.random.cycle_next
// ENUM-SAME: width = 2 : i32
// ENUM: arith.cmpi ult
// ENUM-DAG: arith.constant {{.*}} 1 : i64
// ENUM-DAG: arith.constant {{.*}} 4 : i64
// ENUM-DAG: arith.constant {{.*}} 9 : i64
// ENUM: obelisk_sim.random.solve

// TAGGED: obelisk_sim.class.field
// TAGGED-SAME: !obelisk_sim.packed_union
// TAGGED-DAG: arith.constant {{.*}} 21 : i64
// TAGGED-DAG: arith.constant {{.*}} 60 : i64
// TAGGED-DAG: arith.constant {{.*}} 48 : i64
// TAGGED-DAG: arith.constant {{.*}} 16 : i64
// TAGGED-DAG: arith.constant {{.*}} 63 : i64
// TAGGED-DAG: arith.constant {{.*}} 32 : i64
// TAGGED-DAG: obelisk_sim.random.solve {{.*}} limit
// TAGGED-DAG: obelisk_sim.packed.unflatten

// DIST: arith.constant {{.*}} 6 : i64
// DIST: arith.constant {{.*}} 3 : i64
// DIST-NOT: obelisk_sim.random.bounded
// DIST: obelisk_sim.random.solve
// DIST-SAME: limit

// IEEE 1800-2017 18.3 restricts every active random enum variable to its
// named constants. Preserve that finite domain in the arbitrary-width,
// mode-sensitive residual program as well as in the Z3-proven fast proposal.
// WIDE-ENUM: obelisk_sim.random.solve_wide
// WIDE-ENUM-SAME: program =

// UNSAT: warning: randomize hard constraints are statically unsatisfiable (z3-4.13.4)

//--- enum.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::e", name = "e", node_id = 4 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s4.e"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s5.this"} {}
      }
      obelisk.sv.type.enum_type attributes {hierarchical_name = "E", name = "E", node_id = 6 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s6.E"} {
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b1", hierarchical_name = "E.A", name = "A", node_id = 7 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s7.A"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b100", hierarchical_name = "E.B", name = "B", node_id = 8 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s8.B"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b1001", hierarchical_name = "E.C", name = "C", node_id = 9 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s9.C"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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

//--- wide-enum.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 67 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Wide", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Wide", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Wide>, sym_name = "s3.Wide", this_variable_path = "Wide::this", this_variable_symbol = @s1.$root::@s2::@s3.Wide::@s6.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Wide::bits", name = "bits", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<64 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s4.bits"} {}
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Wide::e", name = "e", node_id = 5 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.enum<"WideE", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s5.e"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Wide::this", is_compiler_generated, is_const, name = "this", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Wide>, sym_name = "s6.this"} {}
      }
      obelisk.sv.type.enum_type attributes {hierarchical_name = "WideE", name = "WideE", node_id = 7 : i64, semantic_type = !obelisk.enum<"WideE", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s7.WideE"} {
        obelisk.sv.symbol.enum_value attributes {constant_value = "2'b1", hierarchical_name = "WideE.A", name = "A", node_id = 8 : i64, semantic_type = !obelisk.enum<"WideE", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s8.A"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "2'b10", hierarchical_name = "WideE.B", name = "B", node_id = 9 : i64, semantic_type = !obelisk.enum<"WideE", !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s9.B"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Wide>, sym_name = "s12.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s10.top::@s11.top} {
              obelisk.sv.expression.named_value attributes {node_id = 16 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s10.top::@s11.top::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Wide>} {}
            }
          }
        }
      }
    }
  }
}

//--- unused-enum.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64, sym_name = "s0.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 1 : i64, sym_name = "s1"} {
      obelisk.sv.type.enum_type attributes {hierarchical_name = "Unused", name = "Unused", node_id = 2 : i64, semantic_type = !obelisk.enum<"Unused", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s2.Unused"} {
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'bx", hierarchical_name = "Unused.X", name = "X", node_id = 3 : i64, semantic_type = !obelisk.enum<"Unused", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s3.X"} {}
      }
    }
  }
}
//--- tagged.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 6 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "T", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "T", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>, sym_name = "s3.T", this_variable_path = "T::this", this_variable_symbol = @s1.$root::@s2::@s3.T::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "T::u", name = "u", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.source_aggregate<"U", true, true, true, false, false, false, 6, 6, 6, 2, [{name = "a", ordinal = 0 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "b", ordinal = 1 : i32, packed_offset = 0 : i64, type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>}, {name = "none", ordinal = 2 : i32, packed_offset = 0 : i64, type = !obelisk.void}]>, sym_name = "s4.u"} {}
        obelisk.sv.symbol.variable attributes {hierarchical_name = "T::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>, sym_name = "s5.this"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 6 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s6.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 7 : i64, sym_name = "s7.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 8 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.T>, sym_name = "s8.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
//--- dist.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::e", name = "e", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s4.e"} {}
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::weighted", name = "weighted", node_id = 17 : i64, sym_name = "s17.weighted", this_variable_path = "C::weighted.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s17.weighted::@s29.this} {
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
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::weighted.this", is_compiler_generated, is_const, name = "this", node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s29.this"} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s5.this"} {}
      }
      obelisk.sv.type.enum_type attributes {hierarchical_name = "E", name = "E", node_id = 6 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s6.E"} {
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b1", hierarchical_name = "E.A", name = "A", node_id = 7 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s7.A"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b100", hierarchical_name = "E.B", name = "B", node_id = 8 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s8.B"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b1001", hierarchical_name = "E.C", name = "C", node_id = 9 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s9.C"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
//--- unsat.mlir

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {}
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 4 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::e", name = "e", node_id = 4 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s4.e"} {}
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::bad", name = "bad", node_id = 17 : i64, sym_name = "s17.bad", this_variable_path = "C::bad.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s17.bad::@s22.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 18 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 19 : i64} {
              obelisk.sv.expression.binary_op attributes {node_id = 20 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {node_id = 21 : i64, referenced_path = "C::e", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.e, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
                obelisk.sv.expression.integer_literal attributes {constant_value = "4'b10", node_id = 22 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {}
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::bad.this", is_compiler_generated, is_const, name = "this", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s22.this"} {}
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s5.this"} {}
      }
      obelisk.sv.type.enum_type attributes {hierarchical_name = "E", name = "E", node_id = 6 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s6.E"} {
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b1", hierarchical_name = "E.A", name = "A", node_id = 7 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s7.A"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b100", hierarchical_name = "E.B", name = "B", node_id = 8 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s8.B"} {}
        obelisk.sv.symbol.enum_value attributes {constant_value = "4'b1001", hierarchical_name = "E.C", name = "C", node_id = 9 : i64, semantic_type = !obelisk.enum<"E", !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s9.C"} {}
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.object"} {}
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
