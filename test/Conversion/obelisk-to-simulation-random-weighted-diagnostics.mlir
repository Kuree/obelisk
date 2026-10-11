// RUN: %split-file %s %t
// RUN: not obelisk-opt %t/guard.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=GUARD
// RUN: not obelisk-opt %t/partial.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=PARTIAL
// RUN: not obelisk-opt %t/default.mlir '--lower-obelisk-to-sim=opt-level=0' 2>&1 | FileCheck %s --check-prefix=DEFAULT
// REQUIRES: runtime-z3
// GUARD: candidate-dependent dist activation requires its inputs to be solved before the complete distributed target
// PARTIAL: dist target split by selected solve before paths requires a conditional weight measure across those paths
// DEFAULT: a default dist specification must use the :/ operator

//--- guard.mlir
module {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 91001 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 91002 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 91003 : i64} {
      obelisk.sv.type.class_type @C attributes {bitstream_width = 3 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !obelisk.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 91004 : i64} {
        obelisk.sv.symbol.class_property @g attributes {hierarchical_name = "C::g", name = "g", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91005 : i64} {
        }
        obelisk.sv.symbol.class_property @t attributes {hierarchical_name = "C::t", name = "t", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91006 : i64} {
        }
        obelisk.sv.symbol.class_property @s attributes {hierarchical_name = "C::s", name = "s", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91007 : i64} {
        }
        obelisk.sv.symbol.constraint_block @constraints attributes {hierarchical_name = "C::constraints", name = "constraints", node_id = 91008 : i64} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 91009 : i64} {
            obelisk.sv.constraint.implication attributes {node_id = 91010 : i64} {
              obelisk.sv.expression.named_value attributes {referenced_path = "C::g", referenced_symbol = @root::@unit::@C::@g, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91011 : i64} {
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 91012 : i64} {
                obelisk.sv.expression.dist attributes {default_weight_kind = 0 : i64, has_default_weight = false, item_count = 2 : i64, item_has_weight = array<i64: 1, 1>, item_weight_kinds = array<i64: 0, 0>, semantic_type = !obelisk.void, node_id = 91013 : i64} {
                  obelisk.sv.expression.named_value attributes {referenced_path = "C::t", referenced_symbol = @root::@unit::@C::@t, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91014 : i64} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91015 : i64} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 91016 : i64} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91017 : i64} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 91018 : i64} {
                  }
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 91019 : i64} {
        }
      }
    }
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 91020 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 91021 : i64} {
        obelisk.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 91022 : i64} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 91023 : i64} {
          }
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", name = "status", lifetime = 1 : i32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 91024 : i64} {
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 91025 : i64} {
          obelisk.sv.statement.list attributes {node_id = 91026 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 91027 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 91028 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@top::@body::@status, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 91029 : i64} {
                }
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 91030 : i64} {
                  obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 91031 : i64} {
                    obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 91032 : i64} {
                      obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91033 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "C::s", referenced_symbol = @root::@unit::@C::@s, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91034 : i64} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 91035 : i64} {
                        }
                      }
                    }
                  }
                  obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 91036 : i64} {
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

//--- partial.mlir
module {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 92037 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 92038 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 92039 : i64} {
      obelisk.sv.type.class_type @C attributes {bitstream_width = 6 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !obelisk.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 92040 : i64} {
        obelisk.sv.symbol.class_property @g attributes {hierarchical_name = "C::g", name = "g", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92041 : i64} {
        }
        obelisk.sv.symbol.class_property @t attributes {hierarchical_name = "C::t", name = "t", rand_mode = 1 : i32, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 92042 : i64} {
        }
        obelisk.sv.symbol.class_property @s attributes {hierarchical_name = "C::s", name = "s", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92043 : i64} {
        }
        obelisk.sv.symbol.constraint_block @constraints attributes {hierarchical_name = "C::constraints", name = "constraints", node_id = 92044 : i64} {
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 92045 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 92046 : i64} {
              obelisk.sv.expression.dist attributes {default_weight_kind = 0 : i64, has_default_weight = false, item_count = 2 : i64, item_has_weight = array<i64: 1, 1>, item_weight_kinds = array<i64: 0, 0>, semantic_type = !obelisk.void, node_id = 92047 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "C::t", referenced_symbol = @root::@unit::@C::@t, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 92048 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 92049 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 92050 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 92051 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 92052 : i64} {
                }
              }
            }
            obelisk.sv.constraint.solve_before attributes {solve_count = 1 : i64, after_count = 1 : i64, node_id = 92053 : i64} {
              obelisk.sv.expression.element_select attributes {semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92054 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "C::t", referenced_symbol = @root::@unit::@C::@t, semantic_type = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, node_id = 92055 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 92056 : i64} {
                }
              }
              obelisk.sv.expression.named_value attributes {referenced_path = "C::g", referenced_symbol = @root::@unit::@C::@g, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92057 : i64} {
              }
            }
          }
        }
        obelisk.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 92058 : i64} {
        }
      }
    }
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 92059 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 92060 : i64} {
        obelisk.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 92061 : i64} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 92062 : i64} {
          }
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", name = "status", lifetime = 1 : i32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 92063 : i64} {
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 92064 : i64} {
          obelisk.sv.statement.list attributes {node_id = 92065 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 92066 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 92067 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@top::@body::@status, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 92068 : i64} {
                }
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 92069 : i64} {
                  obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 92070 : i64} {
                    obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 92071 : i64} {
                      obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92072 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "C::s", referenced_symbol = @root::@unit::@C::@s, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92073 : i64} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 92074 : i64} {
                        }
                      }
                    }
                  }
                  obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 92075 : i64} {
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

//--- default.mlir
module {
  obelisk.sv.symbol.definition @definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 93076 : i64} {
  }
  obelisk.sv.symbol.root @root attributes {hierarchical_name = "$root", name = "$root", node_id = 93077 : i64} {
    obelisk.sv.symbol.compilation_unit @unit attributes {hierarchical_name = "$unit", node_id = 93078 : i64} {
      obelisk.sv.type.class_type @C attributes {bitstream_width = 3 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", semantic_type = !obelisk.class_handle<@root::@unit::@C>, this_variable_path = "C::this", this_variable_symbol = @root::@unit::@C::@this, node_id = 93079 : i64} {
        obelisk.sv.symbol.class_property @g attributes {hierarchical_name = "C::g", name = "g", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93080 : i64} {
        }
        obelisk.sv.symbol.class_property @t attributes {hierarchical_name = "C::t", name = "t", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93081 : i64} {
        }
        obelisk.sv.symbol.class_property @s attributes {hierarchical_name = "C::s", name = "s", rand_mode = 1 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93082 : i64} {
        }
        obelisk.sv.symbol.constraint_block @constraints attributes {hierarchical_name = "C::constraints", name = "constraints", node_id = 93083 : i64} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 93084 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 93085 : i64} {
              obelisk.sv.expression.dist attributes {default_weight_kind = 0 : i64, has_default_weight = true, item_count = 1 : i64, item_has_weight = array<i64: 1>, item_weight_kinds = array<i64: 0>, semantic_type = !obelisk.void, node_id = 93086 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "C::t", referenced_symbol = @root::@unit::@C::@t, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93087 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "0", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93088 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 93089 : i64} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, node_id = 93090 : i64} {
                }
              }
            }
          }
        }
        obelisk.sv.symbol.variable @this attributes {hierarchical_name = "C::this", name = "this", is_compiler_generated, is_const, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 93091 : i64} {
        }
      }
    }
    obelisk.sv.symbol.instance @top attributes {hierarchical_name = "top", name = "top", is_uninstantiated = false, referenced_path = "top", referenced_symbol = @definition, node_id = 93092 : i64} {
      obelisk.sv.symbol.instance_body @body attributes {hierarchical_name = "top", name = "top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 93093 : i64} {
        obelisk.sv.symbol.variable @object attributes {hierarchical_name = "top.object", name = "object", lifetime = 1 : i32, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 93094 : i64} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 93095 : i64} {
          }
        }
        obelisk.sv.symbol.variable @status attributes {hierarchical_name = "top.status", name = "status", lifetime = 1 : i32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 93096 : i64} {
        }
        obelisk.sv.symbol.procedural_block @initial attributes {hierarchical_name = "top", procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64, node_id = 93097 : i64} {
          obelisk.sv.statement.list attributes {node_id = 93098 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 93099 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 93100 : i64} {
                obelisk.sv.expression.named_value attributes {referenced_path = "top.status", referenced_symbol = @root::@top::@body::@status, semantic_type = !obelisk.integral<32, false, false, 31 : 0, int>, node_id = 93101 : i64} {
                }
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_scope_path = "top", system_scope_symbol = @root::@top::@body, node_id = 93102 : i64} {
                  obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 93103 : i64} {
                    obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 93104 : i64} {
                      obelisk.sv.expression.binary_op attributes {operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93105 : i64} {
                        obelisk.sv.expression.named_value attributes {referenced_path = "C::s", referenced_symbol = @root::@unit::@C::@s, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93106 : i64} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, node_id = 93107 : i64} {
                        }
                      }
                    }
                  }
                  obelisk.sv.expression.named_value attributes {referenced_path = "top.object", referenced_symbol = @root::@top::@body::@object, semantic_type = !obelisk.class_handle<@root::@unit::@C>, node_id = 93108 : i64} {
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
