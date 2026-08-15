// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64, sym_name = "s0.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 1 : i64, sym_name = "s1"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 2 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, sym_name = "s2.Base", this_variable_path = "Base::this", this_variable_symbol = @s0.$root::@s1::@s2.Base::@s5.this} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Base::new", is_constructor, name = "new", node_id = 3 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s3.new", this_variable_path = "Base::new.this", this_variable_symbol = @s0.$root::@s1::@s2.Base::@s3.new::@s4.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.formal_argument attributes {direction = 0 : i32, hierarchical_name = "Base::new.value", name = "value", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.value"} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::new.this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, sym_name = "s4.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, sym_name = "s5.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = true, has_cycles = false, hierarchical_name = "Derived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Derived", node_id = 7 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s6.Derived>, sym_name = "s6.Derived", this_variable_path = "Derived::this", this_variable_symbol = @s0.$root::@s1::@s6.Derived::@s10.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Derived::field", name = "field", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.field"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Derived::new", is_constructor, name = "new", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s8.new", this_variable_path = "Derived::new.this", this_variable_symbol = @s0.$root::@s1::@s6.Derived::@s8.new::@s9.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Derived::new.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s6.Derived>, sym_name = "s9.this"} {
          }
        }
        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 12 : i64, referenced_path = "Base::new", referenced_symbol = @s0.$root::@s1::@s2.Base::@s3.new, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Derived::this", is_compiler_generated, is_const, name = "this", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s6.Derived>, sym_name = "s10.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "DefaultDerived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "DefaultDerived", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s11.DefaultDerived>, sym_name = "s11.DefaultDerived", this_variable_path = "DefaultDerived::this", this_variable_symbol = @s0.$root::@s1::@s11.DefaultDerived::@s15.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "DefaultDerived::field", name = "field", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s12.field"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "13", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "DefaultDerived::new", is_constructor, name = "new", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s13.new", this_variable_path = "DefaultDerived::new.this", this_variable_symbol = @s0.$root::@s1::@s11.DefaultDerived::@s13.new::@s14.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.variable attributes {hierarchical_name = "DefaultDerived::new.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s11.DefaultDerived>, sym_name = "s14.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "DefaultDerived::this", is_compiler_generated, is_const, name = "this", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s11.DefaultDerived>, sym_name = "s15.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = true, has_cycles = false, hierarchical_name = "ExplicitSuperDerived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "ExplicitSuperDerived", node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s16.ExplicitSuperDerived>, sym_name = "s16.ExplicitSuperDerived", this_variable_path = "ExplicitSuperDerived::this", this_variable_symbol = @s0.$root::@s1::@s16.ExplicitSuperDerived::@s20.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "ExplicitSuperDerived::field", name = "field", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s17.field"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "17", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "ExplicitSuperDerived::ready", name = "ready", node_id = 37 : i64, semantic_type = !obelisk.event, sym_name = "s21.ready"} {
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "ExplicitSuperDerived::new", is_constructor, name = "new", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.new", this_variable_path = "ExplicitSuperDerived::new.this", this_variable_symbol = @s0.$root::@s1::@s16.ExplicitSuperDerived::@s18.new::@s19.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 26 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 27 : i64} {
              obelisk.sv.expression.new_class attributes {is_super_class = true, node_id = 28 : i64, semantic_type = !obelisk.void} {
                obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = true, is_system_call = false, node_id = 29 : i64, referenced_path = "Base::new", referenced_symbol = @s0.$root::@s1::@s2.Base::@s3.new, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 30 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
            obelisk.sv.statement.expression_statement attributes {node_id = 31 : i64} {
              obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, is_signed = true, node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 33 : i64, referenced_path = "ExplicitSuperDerived::field", referenced_symbol = @s0.$root::@s1::@s16.ExplicitSuperDerived::@s17.field, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "19", is_declared_unsized = true, is_signed = true, node_id = 34 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "ExplicitSuperDerived::new.this", is_compiler_generated, is_const, name = "this", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s16.ExplicitSuperDerived>, sym_name = "s19.this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "ExplicitSuperDerived::this", is_compiler_generated, is_const, name = "this", node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s16.ExplicitSuperDerived>, sym_name = "s20.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 1 : i64, constructor_path = "OutOfBlock::new", constructor_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new::@s25.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "OutOfBlock", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "OutOfBlock", node_id = 38 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s22.OutOfBlock>, sym_name = "s22.OutOfBlock", this_variable_path = "OutOfBlock::this", this_variable_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s27.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "OutOfBlock::enabled", name = "enabled", node_id = 39 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>, sym_name = "s23.enabled"} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = false, node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.method_prototype attributes {extern_implementation_count = 0 : i64, extern_implementation_paths = [], extern_implementation_symbols = [], hierarchical_name = "OutOfBlock::new", is_constructor, name = "new", node_id = 41 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, subroutine_path = "OutOfBlock::new", subroutine_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new::@s25.new, sym_name = "s24.new"} {
          obelisk.sv.symbol.subroutine attributes {hierarchical_name = "OutOfBlock::new", is_constructor, name = "new", node_id = 42 : i64, out_of_block_index = 2 : i64, prototype_path = "OutOfBlock::new", prototype_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s25.new", this_variable_path = "OutOfBlock::new.this", this_variable_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new::@s25.new::@s26.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 43 : i64} {
            }
            obelisk.sv.symbol.variable attributes {hierarchical_name = "OutOfBlock::new.this", is_compiler_generated, is_const, name = "this", node_id = 44 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s22.OutOfBlock>, sym_name = "s26.this"} {
            }
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "OutOfBlock::this", is_compiler_generated, is_const, name = "this", node_id = 45 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s22.OutOfBlock>, sym_name = "s27.this"} {
        }
      }
    }
  }
}

// CHECK: obelisk_sim.func private @[[BASE_NEW:unit_[0-9]+]]({{.*}}!obelisk_sim.class_handle<@[[BASE:__obelisk_class_[^>]+]]>{{.*}}i32
// CHECK: obelisk_sim.func private @{{unit_[0-9]+}}(%[[CONTEXT:arg[0-9]+]]: !obelisk_sim.context{{.*}}, %[[DERIVED_THIS:arg[0-9]+]]: !obelisk_sim.class_handle<@[[DERIVED:__obelisk_class_[^>]+]]>
// CHECK: %[[FIVE:.*]] = arith.constant 5 : i32
// CHECK: %[[BASE_THIS:.*]] = obelisk_sim.class.cast %[[DERIVED_THIS]] : !obelisk_sim.class_handle<@[[DERIVED]]> to !obelisk_sim.class_handle<@[[BASE]]>
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_NEW]] %[[BASE_THIS]](%[[FIVE]])
// CHECK: %[[FIELD:.*]] = obelisk_sim.class.field_ref %[[DERIVED_THIS]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[FIELD]]
// CHECK: obelisk_sim.func private @{{unit_[0-9]+}}({{.*}}%[[DEFAULT_THIS:arg[0-9]+]]: !obelisk_sim.class_handle<@[[DEFAULT_DERIVED:__obelisk_class_[^>]+]]>
// CHECK-SAME: obelisk_sim.hierarchical_name = "DefaultDerived::new"
// CHECK: %[[THREE:.*]] = arith.constant 3 : i32
// CHECK: %[[DEFAULT_BASE_THIS:.*]] = obelisk_sim.class.cast %[[DEFAULT_THIS]] : !obelisk_sim.class_handle<@[[DEFAULT_DERIVED]]> to !obelisk_sim.class_handle<@[[BASE]]>
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_NEW]] %[[DEFAULT_BASE_THIS]](%[[THREE]])
// CHECK: %[[DEFAULT_FIELD:.*]] = obelisk_sim.class.field_ref %[[DEFAULT_THIS]]
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[DEFAULT_FIELD]]
// CHECK: obelisk_sim.func private @{{unit_[0-9]+}}({{.*}}%[[EXPLICIT_THIS:arg[0-9]+]]: !obelisk_sim.class_handle<@[[EXPLICIT_DERIVED:__obelisk_class_[^>]+]]>
// CHECK-SAME: obelisk_sim.hierarchical_name = "ExplicitSuperDerived::new"
// CHECK-DAG: %[[SEVEN:.*]] = arith.constant 7 : i32
// CHECK-DAG: %[[SEVENTEEN:.*]] = arith.constant 17 : i32
// CHECK-DAG: %[[NINETEEN:.*]] = arith.constant 19 : i32
// CHECK: %[[EXPLICIT_BASE_THIS:.*]] = obelisk_sim.class.cast %[[EXPLICIT_THIS]] : !obelisk_sim.class_handle<@[[EXPLICIT_DERIVED]]> to !obelisk_sim.class_handle<@[[BASE]]>
// CHECK-NEXT: obelisk_sim.class.direct_call @[[BASE_NEW]] %[[EXPLICIT_BASE_THIS]](%[[SEVEN]])
// CHECK-NEXT: %[[EXPLICIT_FIELD:.*]] = obelisk_sim.class.field_ref %[[EXPLICIT_THIS]]
// CHECK-NEXT: obelisk_sim.managed.store %[[SEVENTEEN]] to %[[EXPLICIT_FIELD]]
// CHECK-NEXT: %[[READY:.*]] = obelisk_sim.event.create
// CHECK-NEXT: %[[READY_REF:.*]] = obelisk_sim.class.field_ref %[[EXPLICIT_THIS]][{{.*}}] : !obelisk_sim.class_handle<@[[EXPLICIT_DERIVED]]> -> !obelisk_sim.managed_ref<!obelisk_sim.event, @[[EXPLICIT_DERIVED]]>
// CHECK-NEXT: obelisk_sim.managed.store %[[READY]] to %[[READY_REF]] : !obelisk_sim.event, !obelisk_sim.managed_ref<!obelisk_sim.event, @[[EXPLICIT_DERIVED]]>
// CHECK-NEXT: obelisk_sim.managed.store %[[NINETEEN]] to %[[EXPLICIT_FIELD]]
// CHECK-LABEL: obelisk_sim.func private @unit_4
// CHECK-SAME: ({{.*}}%[[OUT_OF_BLOCK_THIS:arg[0-9]+]]: !obelisk_sim.class_handle<@[[OUT_OF_BLOCK:__obelisk_class_[^>]+]]>
// CHECK-SAME: obelisk_sim.hierarchical_name = "OutOfBlock::new"
// CHECK-NEXT: %[[ENABLED:.*]] = arith.constant true
// CHECK-NEXT: %[[ENABLED_FIELD:.*]] = obelisk_sim.class.field_ref %[[OUT_OF_BLOCK_THIS]][@[[OUT_OF_BLOCK]]_field_0]
// CHECK-NEXT: obelisk_sim.managed.store %[[ENABLED]] to %[[ENABLED_FIELD]]
// CHECK-NOT: obelisk_sim.prepared_initializer
