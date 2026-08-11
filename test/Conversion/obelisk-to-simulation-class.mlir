// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "supported_class_use", name = "supported_class_use", node_id = 0 : i64, sym_name = "s0.supported_class_use"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "supported_object", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "supported_object", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>, sym_name = "s3.supported_object", this_variable_path = "supported_object::this", this_variable_symbol = @s1.$root::@s2::@s3.supported_object::@s22.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "supported_object::field", name = "field", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.field"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "supported_object::static_field", lifetime = 1 : i32, name = "static_field", node_id = 40 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.static_field"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::touch", is_static, name = "touch", node_id = 42 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.touch", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 43 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::touch.value", name = "value", node_id = 52 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s25.value"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 5 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s5.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 6 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::pre_randomize", is_builtin, name = "pre_randomize", node_id = 7 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s6.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::post_randomize", is_builtin, name = "post_randomize", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s7.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::get_randstate", is_builtin, name = "get_randstate", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.string, false>, subroutine_kind = 0 : i32, sym_name = "s8.get_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::set_randstate", is_builtin, name = "set_randstate", node_id = 13 : i64, semantic_type = !obelisk.subroutine<(!obelisk.string) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.set_randstate", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 14 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::set_randstate.state", name = "state", node_id = 15 : i64, semantic_type = !obelisk.string, sym_name = "s10.state"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::srandom", is_builtin, name = "srandom", node_id = 16 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s11.srandom", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 17 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::srandom.seed", name = "seed", node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.seed"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::rand_mode", is_builtin, name = "rand_mode", node_id = 19 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.rand_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 20 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::rand_mode.on_ff", name = "on_ff", node_id = 21 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s14.on_ff"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "supported_object::constraint_mode", is_builtin, name = "constraint_mode", node_id = 22 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<1, false, false, 0 : 0, bit>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s15.constraint_mode", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 23 : i64} {
          }
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "supported_object::constraint_mode.on_ff", name = "on_ff", node_id = 24 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s16.on_ff"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_object::this", is_compiler_generated, is_const, name = "this", node_id = 39 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>, sym_name = "s22.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "supported_class_use", is_uninstantiated = false, name = "supported_class_use", node_id = 25 : i64, referenced_path = "supported_class_use", referenced_symbol = @s0.supported_class_use, sym_name = "s17.supported_class_use"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "supported_class_use", name = "supported_class_use", node_id = 26 : i64, sym_name = "s18.supported_class_use"} {
        obelisk.sv.symbol.statement_block attributes {block_kind = 0 : i32, hierarchical_name = "supported_class_use", node_id = 27 : i64, sym_name = "s19"} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "supported_class_use.object", name = "object", node_id = 28 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>, sym_name = "s20.object"} {
            obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "supported_class_use", node_id = 30 : i64, procedure_kind = 0 : i32, sym_name = "s21", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 31 : i64} {
            obelisk.sv.statement.list attributes {node_id = 32 : i64} {
              obelisk.sv.statement.variable_declaration attributes {node_id = 33 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object} {
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 34 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 35 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, node_id = 36 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::field", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s4.field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 37 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "42", node_id = 38 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 44 : i64} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 45 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 64 : i64, is_signed = true, node_id = 46 : i64, packed_offset = 0 : i64, referenced_path = "supported_object::static_field", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s23.static_field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {node_id = 47 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 48 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 49 : i64} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "touch", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 50 : i64, referenced_path = "supported_object::touch", referenced_symbol = @s1.$root::@s2::@s3.supported_object::@s24.touch, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.named_value attributes {node_id = 51 : i64, referenced_path = "supported_class_use.object", referenced_symbol = @s1.$root::@s17.supported_class_use::@s18.supported_class_use::@s19::@s20.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.supported_object>} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "13", is_declared_unsized = true, is_signed = true, node_id = 53 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
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

// CHECK: obelisk_sim.class.decl
// CHECK-SAME: debug_name = "supported_object"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_state"
// CHECK: obelisk_sim.class.field {{.*}}debug_name = "__obelisk_rng_increment"
// CHECK: obelisk_sim.storage.decl {{.*}}hierarchy "supported_object::static_field"
// CHECK: obelisk_sim.class.alloc
// CHECK-NEXT: {{.*}} = obelisk_sim.random.next
// CHECK-NEXT: {{.*}} = obelisk_sim.random.next
// CHECK: obelisk_sim.class.field_ref
// CHECK: obelisk_sim.ref.store
// CHECK: obelisk_sim.call @{{unit_[0-9]+}}({{.*}}) : (!obelisk_sim.context, i32) -> ()
// CHECK-NOT: obelisk.sv.
