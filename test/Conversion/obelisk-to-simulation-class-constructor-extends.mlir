// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0 early-symbol-dce=false' | FileCheck %s

module {
  obelisk.sv.symbol.root @s0.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 0 : i64} {
    obelisk.sv.symbol.compilation_unit @s1 attributes {hierarchical_name = "$unit", node_id = 1 : i64} {
      obelisk.sv.type.class_type @s2.Base attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 2 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, this_variable_path = "Base::this", this_variable_symbol = @s0.$root::@s1::@s2.Base::@s5.this} {
        obelisk.sv.symbol.subroutine @s3.new attributes {hierarchical_name = "Base::new", is_constructor, name = "new", node_id = 3 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "Base::new.this", this_variable_symbol = @s0.$root::@s1::@s2.Base::@s3.new::@s4.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.formal_argument @s4.value attributes {direction = 0 : i32, hierarchical_name = "Base::new.value", name = "value", node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.symbol.variable @s4.this attributes {hierarchical_name = "Base::new.this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>} {
          }
        }
        obelisk.sv.symbol.variable @s5.this attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>} {
        }
      }
      obelisk.sv.type.class_type @s6.Derived attributes {base_class = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = true, has_cycles = false, hierarchical_name = "Derived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Derived", node_id = 7 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s6.Derived>, this_variable_path = "Derived::this", this_variable_symbol = @s0.$root::@s1::@s6.Derived::@s10.this} {
        obelisk.sv.symbol.class_property @s7.field attributes {hierarchical_name = "Derived::field", name = "field", node_id = 8 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "11", is_declared_unsized = true, is_signed = true, node_id = 9 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s8.new attributes {hierarchical_name = "Derived::new", is_constructor, name = "new", node_id = 10 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "Derived::new.this", this_variable_symbol = @s0.$root::@s1::@s6.Derived::@s8.new::@s9.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.variable @s9.this attributes {hierarchical_name = "Derived::new.this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s6.Derived>} {
          }
        }
        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = false, is_super_class = false, is_system_call = false, node_id = 12 : i64, referenced_path = "Base::new", referenced_symbol = @s0.$root::@s1::@s2.Base::@s3.new, semantic_type = !obelisk.void, subroutine_kind = 0 : i32} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "5", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.variable @s10.this attributes {hierarchical_name = "Derived::this", is_compiler_generated, is_const, name = "this", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s6.Derived>} {
        }
      }
      obelisk.sv.type.class_type @s11.DefaultDerived attributes {base_class = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "DefaultDerived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "DefaultDerived", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s11.DefaultDerived>, this_variable_path = "DefaultDerived::this", this_variable_symbol = @s0.$root::@s1::@s11.DefaultDerived::@s15.this} {
        obelisk.sv.symbol.class_property @s12.field attributes {hierarchical_name = "DefaultDerived::field", name = "field", node_id = 17 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "13", is_declared_unsized = true, is_signed = true, node_id = 18 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.subroutine @s13.new attributes {hierarchical_name = "DefaultDerived::new", is_constructor, name = "new", node_id = 19 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "DefaultDerived::new.this", this_variable_symbol = @s0.$root::@s1::@s11.DefaultDerived::@s13.new::@s14.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.variable @s14.this attributes {hierarchical_name = "DefaultDerived::new.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s11.DefaultDerived>} {
          }
        }
        obelisk.sv.symbol.variable @s15.this attributes {hierarchical_name = "DefaultDerived::this", is_compiler_generated, is_const, name = "this", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s11.DefaultDerived>} {
        }
      }
      obelisk.sv.type.class_type @s16.ExplicitSuperDerived attributes {base_class = !obelisk.class_handle<@s0.$root::@s1::@s2.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = true, has_cycles = false, hierarchical_name = "ExplicitSuperDerived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "ExplicitSuperDerived", node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s16.ExplicitSuperDerived>, this_variable_path = "ExplicitSuperDerived::this", this_variable_symbol = @s0.$root::@s1::@s16.ExplicitSuperDerived::@s20.this} {
        obelisk.sv.symbol.class_property @s17.field attributes {hierarchical_name = "ExplicitSuperDerived::field", name = "field", node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "17", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.class_property @s21.ready attributes {hierarchical_name = "ExplicitSuperDerived::ready", name = "ready", node_id = 37 : i64, semantic_type = !obelisk.event} {
        }
        obelisk.sv.symbol.subroutine @s18.new attributes {hierarchical_name = "ExplicitSuperDerived::new", is_constructor, name = "new", node_id = 25 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "ExplicitSuperDerived::new.this", this_variable_symbol = @s0.$root::@s1::@s16.ExplicitSuperDerived::@s18.new::@s19.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
          obelisk.sv.symbol.variable @s19.this attributes {hierarchical_name = "ExplicitSuperDerived::new.this", is_compiler_generated, is_const, name = "this", node_id = 35 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s16.ExplicitSuperDerived>} {
          }
        }
        obelisk.sv.symbol.variable @s20.this attributes {hierarchical_name = "ExplicitSuperDerived::this", is_compiler_generated, is_const, name = "this", node_id = 36 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s16.ExplicitSuperDerived>} {
        }
      }
      obelisk.sv.type.class_type @s22.OutOfBlock attributes {bitstream_width = 1 : i64, constructor_path = "OutOfBlock::new", constructor_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new::@s25.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "OutOfBlock", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "OutOfBlock", node_id = 38 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s22.OutOfBlock>, this_variable_path = "OutOfBlock::this", this_variable_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s27.this} {
        obelisk.sv.symbol.class_property @s23.enabled attributes {hierarchical_name = "OutOfBlock::enabled", name = "enabled", node_id = 39 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_signed = false, node_id = 40 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
          }
        }
        obelisk.sv.symbol.method_prototype @s24.new attributes {extern_implementation_count = 0 : i64, extern_implementation_paths = [], extern_implementation_symbols = [], hierarchical_name = "OutOfBlock::new", is_constructor, name = "new", node_id = 41 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, subroutine_path = "OutOfBlock::new", subroutine_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new::@s25.new} {
          obelisk.sv.symbol.subroutine @s25.new attributes {hierarchical_name = "OutOfBlock::new", is_constructor, name = "new", node_id = 42 : i64, out_of_block_index = 2 : i64, prototype_path = "OutOfBlock::new", prototype_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "OutOfBlock::new.this", this_variable_symbol = @s0.$root::@s1::@s22.OutOfBlock::@s24.new::@s25.new::@s26.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
            obelisk.sv.statement.list attributes {node_id = 43 : i64} {
            }
            obelisk.sv.symbol.variable @s26.this attributes {hierarchical_name = "OutOfBlock::new.this", is_compiler_generated, is_const, name = "this", node_id = 44 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s22.OutOfBlock>} {
            }
          }
        }
        obelisk.sv.symbol.variable @s27.this attributes {hierarchical_name = "OutOfBlock::this", is_compiler_generated, is_const, name = "this", node_id = 45 : i64, semantic_type = !obelisk.class_handle<@s0.$root::@s1::@s22.OutOfBlock>} {
        }
      }
    }
  }
}

// CHECK: simulation.func private @[[BASE_NEW:unit_[0-9]+]]({{.*}}!simulation.class_handle<@[[BASE:__obelisk_class_[^>]+]]>{{.*}}i32
// CHECK: simulation.func private @{{unit_[0-9]+}}(%[[CONTEXT:arg[0-9]+]]: !simulation.context{{.*}}, %[[DERIVED_THIS:arg[0-9]+]]: !simulation.class_handle<@[[DERIVED:__obelisk_class_[^>]+]]>
// CHECK: %[[FIVE:.*]] = arith.constant 5 : i32
// CHECK: %[[BASE_THIS:.*]] = simulation.class.cast %[[DERIVED_THIS]] : !simulation.class_handle<@[[DERIVED]]> to !simulation.class_handle<@[[BASE]]>
// CHECK-NEXT: simulation.class.direct_call @[[BASE_NEW]] %[[BASE_THIS]](%[[FIVE]])
// CHECK: %[[FIELD:.*]] = simulation.class.field_ref %[[DERIVED_THIS]]
// CHECK-NEXT: simulation.managed.store {{.*}} to %[[FIELD]]
// CHECK: simulation.func private @{{unit_[0-9]+}}({{.*}}%[[DEFAULT_THIS:arg[0-9]+]]: !simulation.class_handle<@[[DEFAULT_DERIVED:__obelisk_class_[^>]+]]>
// CHECK-SAME: simulation.hierarchical_name = "DefaultDerived::new"
// CHECK: %[[THREE:.*]] = arith.constant 3 : i32
// CHECK: %[[DEFAULT_BASE_THIS:.*]] = simulation.class.cast %[[DEFAULT_THIS]] : !simulation.class_handle<@[[DEFAULT_DERIVED]]> to !simulation.class_handle<@[[BASE]]>
// CHECK-NEXT: simulation.class.direct_call @[[BASE_NEW]] %[[DEFAULT_BASE_THIS]](%[[THREE]])
// CHECK: %[[DEFAULT_FIELD:.*]] = simulation.class.field_ref %[[DEFAULT_THIS]]
// CHECK-NEXT: simulation.managed.store {{.*}} to %[[DEFAULT_FIELD]]
// CHECK: simulation.func private @{{unit_[0-9]+}}({{.*}}%[[EXPLICIT_THIS:arg[0-9]+]]: !simulation.class_handle<@[[EXPLICIT_DERIVED:__obelisk_class_[^>]+]]>
// CHECK-SAME: simulation.hierarchical_name = "ExplicitSuperDerived::new"
// CHECK-DAG: %[[SEVEN:.*]] = arith.constant 7 : i32
// CHECK-DAG: %[[SEVENTEEN:.*]] = arith.constant 17 : i32
// CHECK-DAG: %[[NINETEEN:.*]] = arith.constant 19 : i32
// CHECK: %[[EXPLICIT_BASE_THIS:.*]] = simulation.class.cast %[[EXPLICIT_THIS]] : !simulation.class_handle<@[[EXPLICIT_DERIVED]]> to !simulation.class_handle<@[[BASE]]>
// CHECK-NEXT: simulation.class.direct_call @[[BASE_NEW]] %[[EXPLICIT_BASE_THIS]](%[[SEVEN]])
// CHECK-NEXT: %[[EXPLICIT_FIELD:.*]] = simulation.class.field_ref %[[EXPLICIT_THIS]]
// CHECK-NEXT: simulation.managed.store %[[SEVENTEEN]] to %[[EXPLICIT_FIELD]]
// CHECK-NEXT: %[[READY:.*]] = simulation.event.create
// CHECK-NEXT: %[[READY_REF:.*]] = simulation.class.field_ref %[[EXPLICIT_THIS]][{{.*}}] : !simulation.class_handle<@[[EXPLICIT_DERIVED]]> -> !simulation.managed_ref<!simulation.event, @[[EXPLICIT_DERIVED]]>
// CHECK-NEXT: simulation.managed.store %[[READY]] to %[[READY_REF]] : !simulation.event, !simulation.managed_ref<!simulation.event, @[[EXPLICIT_DERIVED]]>
// CHECK-NEXT: simulation.managed.store %[[NINETEEN]] to %[[EXPLICIT_FIELD]]
// CHECK-LABEL: simulation.func private @unit_4
// CHECK-SAME: ({{.*}}%[[OUT_OF_BLOCK_THIS:arg[0-9]+]]: !simulation.class_handle<@[[OUT_OF_BLOCK:__obelisk_class_[^>]+]]>
// CHECK-SAME: simulation.hierarchical_name = "OutOfBlock::new"
// CHECK-NEXT: %[[ENABLED:.*]] = arith.constant true
// CHECK-NEXT: %[[ENABLED_FIELD:.*]] = simulation.class.field_ref %[[OUT_OF_BLOCK_THIS]][@[[OUT_OF_BLOCK]]_field_0]
// CHECK-NEXT: simulation.managed.store %[[ENABLED]] to %[[ENABLED_FIELD]]
// CHECK-NOT: simulation.prepared_initializer
