// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.type.class_type @s3.Base attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, this_variable_path = "Base::this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s5.this} {
        obelisk.sv.symbol.class_property @s4.x attributes {hierarchical_name = "Base::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s5.this attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
        }
      }
      obelisk.sv.type.class_type @s6.DerivedA attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "DerivedA", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "DerivedA", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.DerivedA>, this_variable_path = "DerivedA::this", this_variable_symbol = @s1.$root::@s2::@s6.DerivedA::@s8.this} {
        obelisk.sv.symbol.class_property @s7.a attributes {hierarchical_name = "DerivedA::a", name = "a", node_id = 7 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s8.this attributes {hierarchical_name = "DerivedA::this", is_compiler_generated, is_const, name = "this", node_id = 8 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.DerivedA>} {
        }
      }
      obelisk.sv.type.class_type @s9.DerivedB attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "DerivedB", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "DerivedB", node_id = 9 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.DerivedB>, this_variable_path = "DerivedB::this", this_variable_symbol = @s1.$root::@s2::@s9.DerivedB::@s11.this} {
        obelisk.sv.symbol.class_property @s10.b attributes {hierarchical_name = "DerivedB::b", name = "b", node_id = 10 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.variable @s11.this attributes {hierarchical_name = "DerivedB::this", is_compiler_generated, is_const, name = "this", node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.DerivedB>} {
        }
      }
      obelisk.sv.type.class_type @s12.Parent attributes {bitstream_width = 96 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Parent", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Parent", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s12.Parent>, this_variable_path = "Parent::this", this_variable_symbol = @s1.$root::@s2::@s12.Parent::@s15.this} {
        obelisk.sv.symbol.class_property @s13.y attributes {hierarchical_name = "Parent::y", name = "y", node_id = 13 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
        obelisk.sv.symbol.class_property @s14.child attributes {hierarchical_name = "Parent::child", name = "child", node_id = 14 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
        }
        obelisk.sv.symbol.class_property @s20.child2 attributes {hierarchical_name = "Parent::child2", name = "child2", node_id = 23 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>} {
        }
        obelisk.sv.symbol.variable @s15.this attributes {hierarchical_name = "Parent::this", is_compiler_generated, is_const, name = "this", node_id = 15 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s12.Parent>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s16.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 16 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s17.top attributes {hierarchical_name = "top", name = "top", node_id = 17 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable @s18.p attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 18 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s12.Parent>} {
        }
        obelisk.sv.symbol.procedural_block @s19 attributes {hierarchical_name = "top", node_id = 19 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s16.top::@s17.top} {
              obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s16.top::@s17.top::@s18.p, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s12.Parent>} {
              }
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 24 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s16.top::@s17.top} {
              obelisk.sv.expression.named_value attributes {node_id = 26 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s16.top::@s17.top::@s18.p, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s12.Parent>} {
              }
            }
          }
        }
      }
    }
  }
}

// A polymorphic rand handle selects a complete aggregate plan from its current
// dynamic class. Null selects the parent-only plan; no handle is allocated or
// replaced. Concrete alternatives include inherited and derived rand fields.
// CHECK-LABEL: simulation.func private @{{unit_[0-9]+}}{{.*}}simulation.hierarchical_name = "top"
// CHECK-COUNT-2: simulation.class.direct_call @[[HELPER:__obelisk_randomize_plan_[A-Za-z0-9_.$]+]]
// The recursively factored alternatives retain both member dispatches and all
// concrete property plans before the shared outer helper.
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__obelisk_class_s12_Parent_field_2]
// CHECK-DAG: simulation.class.is_instance {{.*}} is @__obelisk_class_s6_DerivedA
// CHECK-DAG: simulation.class.is_instance {{.*}} is @__obelisk_class_s9_DerivedB
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__obelisk_class_s12_Parent_field_0]
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__obelisk_class_s6_DerivedA_field_0]
// CHECK-DAG: simulation.class.field_ref {{.*}}[@__obelisk_class_s9_DerivedB_field_0]
// CHECK-DAG: simulation.managed.store {{.*}} : i32, !simulation.managed_ref<i32, @__obelisk_class_s6_DerivedA>
// CHECK-DAG: simulation.managed.store {{.*}} : i32, !simulation.managed_ref<i32, @__obelisk_class_s9_DerivedB>
// CHECK: simulation.func private @[[HELPER]]
// CHECK: %[[CHILD_REF:.*]] = simulation.class.field_ref {{.*}}[@__obelisk_class_s12_Parent_field_1]
// CHECK-NOT: simulation.managed.store {{.*}} to %[[CHILD_REF]]
// CHECK: %[[CHILD:.*]] = simulation.managed.load %[[CHILD_REF]]
// CHECK-NOT: simulation.managed.store {{.*}} to %[[CHILD_REF]]
// CHECK: %[[NULL:.*]] = simulation.managed.is_null %[[CHILD]]
// CHECK-NOT: simulation.managed.store {{.*}} to %[[CHILD_REF]]
// CHECK: cf.cond_br %[[NULL]], ^[[NULL_PLAN:bb[0-9]+]], ^[[DYNAMIC:bb[0-9]+]]
// The null plan for the first member must still dynamically dispatch the
// second member. This guards the Cartesian plan selection rather than merely
// checking that both fields survive lowering.
// CHECK-NOT: simulation.managed.store {{.*}} to %[[CHILD_REF]]
// CHECK: simulation.class.direct_call @__obelisk_randomize_plan_
// CHECK-NOT: simulation.managed.store {{.*}} to %[[CHILD_REF]]
