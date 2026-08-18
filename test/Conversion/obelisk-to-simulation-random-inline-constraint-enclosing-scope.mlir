// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

// IEEE 1800-2017 18.7: name resolution in an unrestricted inline constraint
// starts in the randomize() with object class and, when a name does not
// resolve there, continues in the scope containing the call. `a.randomize()
// with {j == i;}` inside a method of B therefore reads A's `j` off the
// randomize receiver and B's `i` off the enclosing `this` -- one constraint,
// two receivers.
module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "m", name = "m", node_id = 0 : i64, sym_name = "s0.m"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "A", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "A", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, sym_name = "s3.A", this_variable_path = "A::this", this_variable_symbol = @s1.$root::@s2::@s3.A::@s38.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "A::j", name = "j", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.j"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "A::this", is_compiler_generated, is_const, name = "this", node_id = 76 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, sym_name = "s38.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "B", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "B", node_id = 25 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, sym_name = "s17.B", this_variable_path = "B::this", this_variable_symbol = @s1.$root::@s2::@s17.B::@s39.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "B::a", name = "a", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, sym_name = "s18.a"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "B::i", name = "i", node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s19.i"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "B::r", name = "r", node_id = 28 : i64, semantic_type = !obelisk.subroutine<() -> (), true>, subroutine_kind = 1 : i32, sym_name = "s20.r", this_variable_path = "B::r.this", this_variable_symbol = @s1.$root::@s2::@s17.B::@s20.r::@s21.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 29 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 5, "inline_constraint.sv", 8, 41, "">} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.$unit", system_scope_path = "B::r", system_scope_symbol = @s1.$root::@s2::@s17.B::@s20.r} {
              obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 31 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 30, "inline_constraint.sv", 8, 39, "">} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 32 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 31, "inline_constraint.sv", 8, 38, "">} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 33 : i64, operator_kind = 9 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 31, "inline_constraint.sv", 8, 37, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 34 : i64, referenced_path = "A::j", referenced_symbol = @s1.$root::@s2::@s3.A::@s4.j, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 31, "inline_constraint.sv", 8, 32, "">} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 35 : i64, referenced_path = "B::i", referenced_symbol = @s1.$root::@s2::@s17.B::@s19.i, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 36, "inline_constraint.sv", 8, 37, "">} {
                    }
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 36 : i64, referenced_path = "B::a", referenced_symbol = @s1.$root::@s2::@s17.B::@s18.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, source_range = !obelisk.source_range<"inline_constraint.sv", 8, 11, "inline_constraint.sv", 8, 12, "">} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "B::r.this", is_compiler_generated, is_const, name = "this", node_id = 37 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, sym_name = "s21.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "B::this", is_compiler_generated, is_const, name = "this", node_id = 77 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, sym_name = "s39.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "m", is_uninstantiated = false, name = "m", node_id = 58 : i64, referenced_path = "m", referenced_symbol = @s0.m, sym_name = "s34.m"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "m", name = "m", node_id = 59 : i64, sym_name = "s35.m", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "m.b", lifetime = 1 : i32, name = "b", node_id = 60 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, sym_name = "s36.b"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "m", node_id = 61 : i64, procedure_kind = 0 : i32, sym_name = "s37", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 62 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 11, "inline_constraint.sv", 13, 47, "">} {
            obelisk.sv.statement.list attributes {node_id = 63 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 11, "inline_constraint.sv", 13, 47, "">} {
              obelisk.sv.statement.expression_statement attributes {node_id = 64 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 17, "inline_constraint.sv", 13, 25, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 65 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 17, "inline_constraint.sv", 13, 24, "">} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 66 : i64, referenced_path = "m.b", referenced_symbol = @s1.$root::@s34.m::@s35.m::@s36.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 17, "inline_constraint.sv", 13, 18, "">} {
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 67 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 21, "inline_constraint.sv", 13, 24, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 68 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 26, "inline_constraint.sv", 13, 36, "">} {
                obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = false, node_id = 69 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 26, "inline_constraint.sv", 13, 35, "">} {
                  obelisk.sv.expression.member_access attributes {is_signed = false, member_name = "a", node_id = 70 : i64, referenced_path = "B::a", referenced_symbol = @s1.$root::@s2::@s17.B::@s18.a, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 26, "inline_constraint.sv", 13, 29, "">} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 71 : i64, referenced_path = "m.b", referenced_symbol = @s1.$root::@s34.m::@s35.m::@s36.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 26, "inline_constraint.sv", 13, 27, "">} {
                    }
                  }
                  obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 72 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.A>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 32, "inline_constraint.sv", 13, 35, "">} {
                  }
                }
              }
              obelisk.sv.statement.expression_statement attributes {node_id = 73 : i64, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 37, "inline_constraint.sv", 13, 43, "">} {
                obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "r", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_signed = false, is_super_class = false, is_system_call = false, node_id = 74 : i64, referenced_path = "B::r", referenced_symbol = @s1.$root::@s2::@s17.B::@s20.r, semantic_type = !obelisk.void, subroutine_kind = 1 : i32} {
                  obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 75 : i64, referenced_path = "m.b", referenced_symbol = @s1.$root::@s34.m::@s35.m::@s36.b, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.B>, source_range = !obelisk.source_range<"inline_constraint.sv", 13, 37, "inline_constraint.sv", 13, 38, "">} {
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


// CHECK-LABEL: obelisk_sim.func private @unit_0
// The randomize receiver is loaded out of the enclosing object, and the
// constraint reads A's `j` off it while B's `i` still comes off `this`.
// CHECK: obelisk_sim.class.field_ref %arg1[@__obelisk_class_s17_B_field_0] : !obelisk_sim.class_handle<@__obelisk_class_s17_B> -> !obelisk_sim.managed_ref<!obelisk_sim.class_handle<@__obelisk_class_s3_A>, @__obelisk_class_s17_B>
// CHECK: %[[RECEIVER:.*]] = obelisk_sim.managed.load
// CHECK: obelisk_sim.class.field_ref %[[RECEIVER]][@__obelisk_class_s3_A_field_0] : !obelisk_sim.class_handle<@__obelisk_class_s3_A> -> !obelisk_sim.managed_ref<i32, @__obelisk_class_s3_A>
// CHECK: obelisk_sim.class.field_ref %arg1[@__obelisk_class_s17_B_field_1] : !obelisk_sim.class_handle<@__obelisk_class_s17_B> -> !obelisk_sim.managed_ref<i32, @__obelisk_class_s17_B>
