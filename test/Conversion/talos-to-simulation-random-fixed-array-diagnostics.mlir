// RUN: not talos-opt %s --split-input-file '--lower-talos-to-sim=opt-level=0' 2>&1 | FileCheck %s
// IEEE 1800-2023 18.5.9 requires integral or real solve-before operands;
// a whole unpacked array is not integral. The implementation's 32-bit randc
// limit applies independently to each array leaf under 18.4.2.
// CHECK-DAG: error: solve before operands must be integral random values
// CHECK-DAG: error: randc array leaves must be integral values no wider than 32 bits

module {
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 60001 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 60002 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 60003 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 12 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 60004 : i64} {
        talos.sv.symbol.class_property @values attributes {hierarchical_name = "C::values", name = "values", rand_mode = 1 : i32, semantic_type = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 60005 : i64} {
        }
        talos.sv.symbol.class_property @pick attributes {hierarchical_name = "C::pick", name = "pick", rand_mode = 1 : i32, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60006 : i64} {
        }
        talos.sv.symbol.constraint_block @constraints attributes {hierarchical_name = "C::constraints", name = "constraints", node_id = 60007 : i64} {
          talos.sv.constraint.list attributes {item_count = 1 : i64, node_id = 60008 : i64} {
            talos.sv.constraint.solve_before attributes {solve_count = 1 : i64, after_count = 1 : i64, node_id = 60009 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "C::values", referenced_symbol = @root::@unit::@C::@values, semantic_type = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 60010 : i64} {
              }
              talos.sv.expression.named_value attributes {referenced_path = "C::pick", referenced_symbol = @root::@unit::@C::@pick, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60011 : i64} {
              }
            }
          }
        }
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60012 : i64} {
        }
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 60013 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 60014 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60015 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60016 : i64} {
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 60017 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 60018 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 60019 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60020 : i64} {
              }
            }
          }
        }
      }
    }
  }
}

// -----

module {
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 60021 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 60022 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 60023 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 70 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 60024 : i64} {
        talos.sv.symbol.class_property @values attributes {hierarchical_name = "C::values", name = "values", rand_mode = 2 : i32, semantic_type = !talos.ranged_unpacked_array<1 : 0 x !talos.ranged_packed_array<32 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 60025 : i64} {
        }
        talos.sv.symbol.class_property @pick attributes {hierarchical_name = "C::pick", name = "pick", rand_mode = 1 : i32, semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 60026 : i64} {
        }
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60027 : i64} {
        }
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 60028 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 60029 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60030 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60031 : i64} {
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 60032 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 60033 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 60034 : i64} {
              talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 60035 : i64} {
              }
            }
          }
        }
      }
    }
  }
}
