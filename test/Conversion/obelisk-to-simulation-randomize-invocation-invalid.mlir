// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/property-list.mlir --obelisk-sim-prepare -o /dev/null 2>&1 | FileCheck %s --check-prefix=PROPERTY-LIST
// RUN: not obelisk-opt %t/std-randomize.mlir --obelisk-sim-prepare -o /dev/null 2>&1 | FileCheck %s --check-prefix=STD-RANDOMIZE

// PROPERTY-LIST: error: randomize property argument lists are outside the executable object-randomization boundary
// STD-RANDOMIZE: error: std::randomize is outside the executable object-randomization boundary

//--- property-list.mlir

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 1 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s5.this"} {
        }
      }
      obelisk.sv.symbol.variable attributes {hierarchical_name = "object", lifetime = 1 : i32, name = "object", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s6.object"} {
      }
      obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 7 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
        obelisk.sv.constraint.list attributes {item_count = 0 : i64, node_id = 10 : i64} {
        }
        obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "object", referenced_symbol = @s1.$root::@s2::@s6.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
        }
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
    }
  }
}

//--- std-randomize.mlir

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, node_id = 3 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
        obelisk.sv.expression.integer_literal attributes {constant_value = "0", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
    }
  }
}
