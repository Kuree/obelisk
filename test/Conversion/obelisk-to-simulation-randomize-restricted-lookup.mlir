// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 18.7 says that an explicit empty identifier list prevents
// unqualified names from beginning lookup in the randomized object's class.
// The semantic frontend must therefore resolve both shadowed bounds to these
// function formals. Verify that lowering specializes both captured formals to
// the call arguments 1 and 9 rather than loading the class property named
// "lo", whose value is -100.
// CHECK-LABEL: obelisk_sim.func private @{{.*}}({{.*}}%arg2: i32{{.*}}%arg3: i32{{.*}}obelisk_sim.hierarchical_name = "restricted_empty"
// CHECK-NOT: arith.constant -100
// CHECK: arith.constant {obelisk_sim.rematerialized} 1 : i64
// CHECK: arith.constant {obelisk_sim.rematerialized} 9 : i64
// CHECK: obelisk_sim.random.solve {{.*}} captures(%c1_i64_{{[0-9]+}}, %c9_i64_{{[0-9]+}})

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::value", name = "value", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.value"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::lo", name = "lo", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.lo"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "-100", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::randomize", is_builtin, is_declared_virtual, is_randomize, is_virtual, name = "randomize", node_id = 7 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s7.randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 8 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::pre_randomize", is_builtin, name = "pre_randomize", node_id = 9 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.pre_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 10 : i64} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "C::post_randomize", is_builtin, name = "post_randomize", node_id = 11 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s9.post_randomize", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 12 : i64} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 13 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.this"} {
        }
      }
      obelisk.sv.symbol.subroutine attributes {hierarchical_name = "restricted_empty", name = "restricted_empty", node_id = 14 : i64, return_variable_path = "restricted_empty.restricted_empty", return_variable_symbol = @s1.$root::@s2::@s11.restricted_empty::@s15.restricted_empty, semantic_type = !obelisk.subroutine<(!obelisk.class_handle<@s1.$root::@s2::@s3.C>, !obelisk.integral<32, true, false, 31 : 0, int>, !obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.integral<32, true, false, 31 : 0, int>, false>, subroutine_kind = 0 : i32, sym_name = "s11.restricted_empty", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.statement.return attributes {node_id = 15 : i64} {
          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "restricted_empty", system_scope_symbol = @s1.$root::@s2::@s11.restricted_empty} {
            obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 17 : i64} {
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 18 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 19 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 63 : i64, is_signed = true, node_id = 20 : i64, packed_offset = 4294967296 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 21 : i64, referenced_path = "restricted_empty.object", referenced_symbol = @s1.$root::@s2::@s11.restricted_empty::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "restricted_empty.lo", referenced_symbol = @s1.$root::@s2::@s11.restricted_empty::@s13.lo, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
              obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 23 : i64} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 24 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.member_access attributes {field_ordinal = 63 : i64, is_signed = true, node_id = 25 : i64, packed_offset = 4294967296 : i64, referenced_path = "C::value", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.value, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "restricted_empty.object", referenced_symbol = @s1.$root::@s2::@s11.restricted_empty::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 27 : i64, referenced_path = "restricted_empty.hi", referenced_symbol = @s1.$root::@s2::@s11.restricted_empty::@s14.hi, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 28 : i64, referenced_path = "restricted_empty.object", referenced_symbol = @s1.$root::@s2::@s11.restricted_empty::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
            }
          }
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "restricted_empty.object", name = "object", node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.object"} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "restricted_empty.lo", name = "lo", node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s13.lo"} {
        }
        obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "restricted_empty.hi", name = "hi", node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s14.hi"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "restricted_empty.restricted_empty", is_compiler_generated, name = "restricted_empty", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s15.restricted_empty"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 33 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s16.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 34 : i64, sym_name = "s17.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s18.object"} {
          obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 37 : i64, procedure_kind = 0 : i32, sym_name = "s19", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 38 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "restricted_empty", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = false, node_id = 39 : i64, referenced_path = "restricted_empty", referenced_symbol = @s1.$root::@s2::@s11.restricted_empty, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 40 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s16.top::@s17.top::@s18.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = true, node_id = 41 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.integer_literal attributes {constant_value = "9", is_signed = true, node_id = 42 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}
