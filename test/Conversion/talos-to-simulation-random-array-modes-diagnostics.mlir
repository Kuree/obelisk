// RUN: not talos-opt %s --lower-talos-to-sim --split-input-file 2>&1 | FileCheck %s
// LRM 18.8: an array query requires a singular element; mode operations
// require a variable declared rand or randc.
// CHECK-COUNT-2: error: rand_mode() queries require a singular array element
// CHECK: error: rand_mode selects a variable that is not declared rand or randc

module {
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100001 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 100002 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 100003 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, hierarchical_name = "C", name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 100004 : i64} {
        talos.sv.symbol.class_property @m attributes {hierarchical_name = "C::m", name = "m", rand_mode = 1 : i32, semantic_type = !talos.ranged_unpacked_array<-1 : 0 x !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>>, node_id = 100005 : i64} {
        }
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100006 : i64} {
        }
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 100007 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 100008 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100009 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100010 : i64} {
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 100011 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 100012 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 100013 : i64} {
              talos.sv.expression.member_access attributes {field_ordinal = 0 : i64, packed_offset = 0 : i64, referenced_path = "C::m", referenced_symbol = @root::@unit::@C::@m, semantic_type = !talos.ranged_unpacked_array<-1 : 0 x !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>>, node_id = 100014 : i64} {
                talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100015 : i64} {
                }
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
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100016 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 100017 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 100018 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, hierarchical_name = "C", name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 100019 : i64} {
        talos.sv.symbol.class_property @m attributes {hierarchical_name = "C::m", name = "m", rand_mode = 1 : i32, semantic_type = !talos.ranged_unpacked_array<-1 : 0 x !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>>, node_id = 100020 : i64} {
        }
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100021 : i64} {
        }
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 100022 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 100023 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100024 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100025 : i64} {
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 100026 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 100027 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 100028 : i64} {
              talos.sv.expression.element_select attributes {semantic_type = !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 100029 : i64} {
                talos.sv.expression.member_access attributes {field_ordinal = 0 : i64, packed_offset = 0 : i64, referenced_path = "C::m", referenced_symbol = @root::@unit::@C::@m, semantic_type = !talos.ranged_unpacked_array<-1 : 0 x !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>>, node_id = 100030 : i64} {
                  talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100031 : i64} {
                  }
                }
                talos.sv.expression.integer_literal attributes {constant_value = "-1", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 100032 : i64} {
                }
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
  talos.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 100033 : i64} {
  }
  talos.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 100034 : i64} {
    talos.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 100035 : i64} {
      talos.sv.type.class_type @C attributes {bitstream_width = 16 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, hierarchical_name = "C", name = "C", semantic_type = !talos.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 100036 : i64} {
        talos.sv.symbol.class_property @m attributes {hierarchical_name = "C::m", name = "m", semantic_type = !talos.ranged_unpacked_array<-1 : 0 x !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>>, node_id = 100037 : i64} {
        }
        talos.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100038 : i64} {
        }
      }
    }
    talos.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 100039 : i64} {
      talos.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 100040 : i64} {
        talos.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100041 : i64} {
          talos.sv.expression.new_class attributes {is_super_class = false, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100042 : i64} {
          }
        }
        talos.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 100043 : i64} {
          talos.sv.statement.expression_statement attributes {node_id = 100044 : i64} {
            talos.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "rand_mode", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !talos.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 100045 : i64} {
              talos.sv.expression.element_select attributes {semantic_type = !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>, node_id = 100046 : i64} {
                talos.sv.expression.element_select attributes {semantic_type = !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>, node_id = 100047 : i64} {
                  talos.sv.expression.member_access attributes {field_ordinal = 0 : i64, packed_offset = 0 : i64, referenced_path = "C::m", referenced_symbol = @root::@unit::@C::@m, semantic_type = !talos.ranged_unpacked_array<-1 : 0 x !talos.ranged_unpacked_array<3 : 2 x !talos.ranged_packed_array<3 : 0 x !talos.integral<1, false, false, 0 : 0, bit>>>>, node_id = 100048 : i64} {
                    talos.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !talos.class_handle<@root::@unit::@C>, node_id = 100049 : i64} {
                    }
                  }
                  talos.sv.expression.integer_literal attributes {constant_value = "-1", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 100050 : i64} {
                  }
                }
                talos.sv.expression.integer_literal attributes {constant_value = "3", semantic_type = !talos.integral<32, true, false, 31 : 0, int>, node_id = 100051 : i64} {
                }
              }
            }
          }
        }
      }
    }
  }
}
