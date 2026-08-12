// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines -o /dev/null
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   '--encode-obelisk-sim-to-bytecode=vpi=off' -o /dev/null

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 96 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s9.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::a", name = "a", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.a"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::b", name = "b", node_id = 5 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s5.b"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::c", name = "c", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.c"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::keep", name = "keep", node_id = 7 : i64, sym_name = "s7.keep", this_variable_path = "C::keep.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.keep::@s8.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 8 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 9 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 10 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 11 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  obelisk.sv.expression.binary_op attributes {is_signed = true, node_id = 12 : i64, operator_kind = 0 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 13 : i64, referenced_path = "C::a", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.a, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 14 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.b, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                  obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 15 : i64, referenced_path = "C::c", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.c, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "20", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::keep.this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s8.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s9.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, bitstream_width = 128 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 100 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.D>, sym_name = "s20.D", this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s20.D::@s23.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "D::d", name = "d", node_id = 101 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s21.d"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "D::derived_keep", name = "derived_keep", node_id = 102 : i64, sym_name = "s22.derived_keep", this_variable_path = "D::derived_keep.this", this_variable_symbol = @s1.$root::@s2::@s20.D::@s22.derived_keep::@s24.this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 103 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 104 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 105 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 106 : i64, referenced_path = "D::d", referenced_symbol = @s1.$root::@s2::@s20.D::@s21.d, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "30", is_declared_unsized = true, is_signed = true, node_id = 107 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "D::derived_keep.this", is_compiler_generated, is_const, name = "this", node_id = 108 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.D>, sym_name = "s24.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 109 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s20.D>, sym_name = "s23.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 19 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 20 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s12.object"} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 3 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s10.top::@s11.top} {
              obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 110 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 111 : i64} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 112 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 113 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.b, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "10", is_declared_unsized = true, is_signed = true, node_id = 114 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 26 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s10.top::@s11.top::@s12.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 27 : i64, referenced_path = "C::b", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.b, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 28 : i64, referenced_path = "C::c", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.c, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}

// The explicit list is the complete temporary random-variable set. The
// ordinary rand property `a` is loaded as constraint state, `b` keeps its
// declared randc behavior, and non-rand `c` becomes random for this call.
// A base-typed receiver gets derived-first dynamic plans; the unlisted derived
// rand property `d` is state in the derived plan. The inline `b < 10`
// constraint survives property-name removal and cloning into both plans. The
// object's persistent rand_mode mask is deliberately not read.

// CHECK: obelisk_sim.class.field @__obelisk_class_s3_C_field_0 {{.*}} {debug_name = "a"
// CHECK: obelisk_sim.class.field @__obelisk_class_s3_C_field_1 {{.*}} {debug_name = "b"
// CHECK: obelisk_sim.class.field @__obelisk_class_s3_C_field_1_randc_key
// CHECK: obelisk_sim.class.field @__obelisk_class_s3_C_field_1_randc_position
// CHECK: obelisk_sim.class.field @__obelisk_class_s3_C_field_4 {{.*}} {debug_name = "c"
// CHECK: obelisk_sim.class.field @__obelisk_class_s20_D_field_0 {{.*}} {debug_name = "d"
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK: %[[OBJECT:.*]] = obelisk_sim.ref.load {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_C>
// CHECK: %[[IS_D:.*]] = obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s20_D
// CHECK: cf.cond_br %[[IS_D]]
// CHECK: %[[DERIVED:.*]] = obelisk_sim.class.cast %[[OBJECT]] {{.*}} to !obelisk_sim.class_handle<@__obelisk_class_s20_D>
// CHECK-NEXT: %[[D_B_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_C_field_1]
// CHECK-NEXT: %[[D_KEY_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_C_field_1_randc_key]
// CHECK-NEXT: %[[D_POS_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_C_field_1_randc_position]
// CHECK-NEXT: %[[D_C_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_C_field_4]
// CHECK: %[[IS_C:.*]] = obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s3_C
// CHECK: %[[D_A_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_C_field_0]
// CHECK-NEXT: obelisk_sim.managed.load %[[D_A_REF]]
// CHECK: %[[D_STATE_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s20_D_field_0]
// CHECK-NEXT: obelisk_sim.managed.load %[[D_STATE_REF]]
// CHECK: %[[D_INLINE_LIMIT:.*]] = arith.constant {{.*}}10 : i32
// CHECK-NEXT: {{.*}} = arith.cmpi slt, {{.*}}, %[[D_INLINE_LIMIT]] : i32
// CHECK: obelisk_sim.managed.store {{.*}} to %[[D_B_REF]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[D_KEY_REF]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[D_POS_REF]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[D_C_REF]]
// CHECK: %[[C_B_REF:.*]] = obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_1]
// CHECK-NEXT: %[[C_KEY_REF:.*]] = obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_1_randc_key]
// CHECK-NEXT: %[[C_POS_REF:.*]] = obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_1_randc_position]
// CHECK-NEXT: %[[C_C_REF:.*]] = obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_4]
// CHECK: %[[C_A_REF:.*]] = obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_0]
// CHECK-NEXT: obelisk_sim.managed.load %[[C_A_REF]]
// CHECK: %[[C_INLINE_LIMIT:.*]] = arith.constant {{.*}}10 : i32
// CHECK-NEXT: {{.*}} = arith.cmpi slt, {{.*}}, %[[C_INLINE_LIMIT]] : i32
// CHECK: obelisk_sim.managed.store {{.*}} to %[[C_B_REF]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[C_KEY_REF]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[C_POS_REF]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[C_C_REF]]
// CHECK-NOT: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s3_C_field___obelisk_rand_mode]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[D_A_REF]]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[D_STATE_REF]]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[C_A_REF]]
