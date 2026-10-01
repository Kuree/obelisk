// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 7.4.6 makes one element of an unpacked array an assignment
// target whether or not its index is constant, and 8.3 makes a class property
// a variable of the object. The object's storage is managed and exposes no
// stable interior reference, so a variable index reads the whole property,
// replaces the addressed element, and stores the array back — the same
// read-modify-write a constant index already gets.

module {
  obelisk.sv.symbol.definition @s0.class_property_dynamic_element attributes {definition_kind = 0 : i32, hierarchical_name = "class_property_dynamic_element", name = "class_property_dynamic_element", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
    }
    obelisk.sv.symbol.instance @s3.class_property_dynamic_element attributes {hierarchical_name = "class_property_dynamic_element", is_uninstantiated = false, name = "class_property_dynamic_element", node_id = 3 : i64, referenced_path = "class_property_dynamic_element", referenced_symbol = @s0.class_property_dynamic_element} {
      obelisk.sv.symbol.instance_body @s4.class_property_dynamic_element attributes {hierarchical_name = "class_property_dynamic_element", name = "class_property_dynamic_element", node_id = 4 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.type.class_type @s5.Cls attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "class_property_dynamic_element.Cls", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Cls", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>, this_variable_path = "class_property_dynamic_element.Cls::this", this_variable_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s5.Cls::@s25.this} {
          obelisk.sv.symbol.class_property @s6.arr attributes {hierarchical_name = "class_property_dynamic_element.Cls::arr", name = "arr", node_id = 6 : i64, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
          }
          obelisk.sv.symbol.subroutine @s7.put attributes {hierarchical_name = "class_property_dynamic_element.Cls::put", name = "put", node_id = 7 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "class_property_dynamic_element.Cls::put.this", this_variable_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s5.Cls::@s7.put::@s10.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 8 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.element_select attributes {is_signed = true, node_id = 10 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 11 : i64, referenced_path = "class_property_dynamic_element.Cls::arr", referenced_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s5.Cls::@s6.arr, semantic_type = !obelisk.ranged_unpacked_array<0 : 1 x !obelisk.integral<32, true, false, 31 : 0, int>>} {
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 12 : i64, referenced_path = "class_property_dynamic_element.Cls::put.index", referenced_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s5.Cls::@s7.put::@s8.index, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "class_property_dynamic_element.Cls::put.value", referenced_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s5.Cls::@s7.put::@s9.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
            obelisk.sv.symbol.formal_argument @s8.index attributes {direction = 0 : i32, hierarchical_name = "class_property_dynamic_element.Cls::put.index", name = "index", node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.symbol.formal_argument @s9.value attributes {direction = 0 : i32, hierarchical_name = "class_property_dynamic_element.Cls::put.value", name = "value", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
            obelisk.sv.symbol.variable @s10.this attributes {hierarchical_name = "class_property_dynamic_element.Cls::put.this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
            }
          }
          obelisk.sv.symbol.subroutine @s11.randomize attributes {hierarchical_name = "class_property_dynamic_element.Cls::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 17 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 18 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s12.pre_randomize attributes {hierarchical_name = "class_property_dynamic_element.Cls::pre_randomize", is_builtin, name = "pre_randomize", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 20 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s13.post_randomize attributes {hierarchical_name = "class_property_dynamic_element.Cls::post_randomize", is_builtin, name = "post_randomize", node_id = 21 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 22 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s14.get_randstate attributes {hierarchical_name = "class_property_dynamic_element.Cls::get_randstate", is_builtin, name = "get_randstate", node_id = 23 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 24 : i64} {
            }
          }
          obelisk.sv.symbol.subroutine @s15.set_randstate attributes {hierarchical_name = "class_property_dynamic_element.Cls::set_randstate", is_builtin, name = "set_randstate", node_id = 25 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 26 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s16.state attributes {direction = 0 : i32, hierarchical_name = "class_property_dynamic_element.Cls::set_randstate.state", name = "state", node_id = 27 : i64, semantic_type = !obelisk.string} {
            }
          }
          obelisk.sv.symbol.subroutine @s17.srandom attributes {hierarchical_name = "class_property_dynamic_element.Cls::srandom", is_builtin, name = "srandom", node_id = 28 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 29 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s18.seed attributes {direction = 0 : i32, hierarchical_name = "class_property_dynamic_element.Cls::srandom.seed", name = "seed", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.subroutine @s19.rand_mode attributes {hierarchical_name = "class_property_dynamic_element.Cls::rand_mode", is_builtin, name = "rand_mode", node_id = 31 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 32 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s20.on_ff attributes {direction = 0 : i32, hierarchical_name = "class_property_dynamic_element.Cls::rand_mode.on_ff", name = "on_ff", node_id = 33 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.subroutine @s21.constraint_mode attributes {hierarchical_name = "class_property_dynamic_element.Cls::constraint_mode", is_builtin, name = "constraint_mode", node_id = 34 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 35 : i64} {
            }
            obelisk.sv.symbol.formal_argument @s22.on_ff attributes {direction = 0 : i32, hierarchical_name = "class_property_dynamic_element.Cls::constraint_mode.on_ff", name = "on_ff", node_id = 36 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
            }
          }
          obelisk.sv.symbol.variable @s25.this attributes {hierarchical_name = "class_property_dynamic_element.Cls::this", is_compiler_generated, is_const, name = "this", node_id = 50 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
          }
        }
        obelisk.sv.symbol.variable @s23.c attributes {hierarchical_name = "class_property_dynamic_element.c", lifetime = 1 : i32, name = "c", node_id = 37 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
        }
        obelisk.sv.symbol.procedural_block @s24 attributes {hierarchical_name = "class_property_dynamic_element", node_id = 38 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 39 : i64} {
            obelisk.sv.statement.list attributes {node_id = 40 : i64} {
              obelisk.sv.statement.expression_statement attributes {node_id = 41 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 42 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 43 : i64, referenced_path = "class_property_dynamic_element.c", referenced_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s23.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 44 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 45 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 2 : i64, callee_name = "put", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 46 : i64, referenced_path = "class_property_dynamic_element.Cls::put", referenced_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s5.Cls::@s7.put, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 47 : i64, referenced_path = "class_property_dynamic_element.c", referenced_symbol = @s1.$root::@s3.class_property_dynamic_element::@s4.class_property_dynamic_element::@s23.c, semantic_type = !obelisk.class_handle<@s1.$root::@s4.class_property_dynamic_element::@s5.Cls>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 49 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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


// CHECK-LABEL: simulation.func private @unit_0
// CHECK:      %[[FIELD:.*]] = simulation.class.field_ref {{.*}}@{{.*}}arr
// CHECK:      %[[ARRAY:.*]] = simulation.managed.load %[[FIELD]]
// CHECK:      %[[UPDATED:.*]] = simulation.array.insert_dynamic {{.*}} into %[[ARRAY]]{{\[}}%{{.*}}]
// CHECK:      simulation.managed.store %[[UPDATED]] to %[[FIELD]]
