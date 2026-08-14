// RUN: obelisk-opt %s --obelisk-sim-prepare \
// RUN:   | FileCheck %s --check-prefix=PREPARED \
// RUN:       --implicit-check-not='randomize_nested_plans = [{class = @__obelisk_class_s5_Inert'
// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' \
// RUN:   | FileCheck %s --check-prefix=LOWERED \
// RUN:       --implicit-check-not='class.is_instance {{.*}} is @__obelisk_class_s5_Inert'

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = true, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s3.Base", this_variable_path = "Base::this", this_variable_symbol = @s1.$root::@s2::@s3.Base::@s4.base_this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 4 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s4.base_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Inert", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Inert", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s5.Inert>, sym_name = "s5.Inert", this_variable_path = "Inert::this", this_variable_symbol = @s1.$root::@s2::@s5.Inert::@s6.inert_this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Inert::this", is_compiler_generated, is_const, name = "this", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s5.Inert>, sym_name = "s6.inert_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Active", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Active", node_id = 7 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s7.Active>, sym_name = "s7.Active", this_variable_path = "Active::this", this_variable_symbol = @s1.$root::@s2::@s7.Active::@s9.active_this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Active::x", name = "x", node_id = 8 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s8.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Active::this", is_compiler_generated, is_const, name = "this", node_id = 9 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s7.Active>, sym_name = "s9.active_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Constrained", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Constrained", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s10.Constrained>, sym_name = "s10.Constrained", this_variable_path = "Constrained::this", this_variable_symbol = @s1.$root::@s2::@s10.Constrained::@s14.constrained_this} {
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "Constrained::present", name = "present", node_id = 11 : i64, sym_name = "s11.present", this_variable_path = "Constrained::present.this", this_variable_symbol = @s1.$root::@s2::@s10.Constrained::@s11.present::@s13.constraint_this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 12 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 13 : i64} {
              obelisk.sv.expression.integer_literal attributes {constant_value = "1", node_id = 14 : i64, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Constrained::present.this", is_compiler_generated, is_const, name = "this", node_id = 15 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s10.Constrained>, sym_name = "s13.constraint_this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Constrained::this", is_compiler_generated, is_const, name = "this", node_id = 16 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s10.Constrained>, sym_name = "s14.constrained_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Hooked", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Hooked", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Hooked>, sym_name = "s17.Hooked", this_variable_path = "Hooked::this", this_variable_symbol = @s1.$root::@s2::@s17.Hooked::@s20.hooked_this} {
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Hooked::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 18 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s18.pre_randomize", this_variable_path = "Hooked::pre_randomize.this", this_variable_symbol = @s1.$root::@s2::@s17.Hooked::@s18.pre_randomize::@s19.hook_this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 19 : i64} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Hooked::pre_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 20 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Hooked>, sym_name = "s19.hook_this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Hooked::this", is_compiler_generated, is_const, name = "this", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s17.Hooked>, sym_name = "s20.hooked_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Parent", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Parent", node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.Parent>, sym_name = "s22.Parent", this_variable_path = "Parent::this", this_variable_symbol = @s1.$root::@s2::@s22.Parent::@s25.parent_this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::y", name = "y", node_id = 23 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s23.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::child", name = "child", node_id = 24 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Base>, sym_name = "s24.child"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Parent::this", is_compiler_generated, is_const, name = "this", node_id = 25 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.Parent>, sym_name = "s25.parent_this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 26 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s26.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 27 : i64, sym_name = "s27.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 28 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.Parent>, sym_name = "s28.p"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 29 : i64, procedure_kind = 0 : i32, sym_name = "s29", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 31 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s26.top::@s27.top} {
              obelisk.sv.expression.named_value attributes {node_id = 32 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s26.top::@s27.top::@s28.p, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s22.Parent>} {
              }
            }
          }
        }
      }
    }
  }
}

// IEEE 1800-2017 18.5.9 makes non-null active rand objects part of one solve.
// A dynamic object with no rand state, constraints, or lifecycle hooks adds
// neither a variable nor a predicate nor an observable call. It shares the
// null/default branch instead of receiving a separate frozen plan.
// PREPARED-DAG: obelisk_sim.randomize_nested_plans = [{class = @__obelisk_class_s7_Active, field = @__obelisk_class_s22_Parent_field_1}]
// PREPARED-DAG: obelisk_sim.randomize_nested_plans = [{class = @__obelisk_class_s10_Constrained, field = @__obelisk_class_s22_Parent_field_1}]
// PREPARED-DAG: obelisk_sim.randomize_nested_plans = [{class = @__obelisk_class_s17_Hooked, field = @__obelisk_class_s22_Parent_field_1}]
// PREPARED: obelisk_sim.randomize_nested_plans = [{field = @__obelisk_class_s22_Parent_field_1, null}]

// LOWERED-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// LOWERED-DAG: obelisk_sim.class.is_instance {{.*}} is @__obelisk_class_s7_Active
// LOWERED-DAG: obelisk_sim.class.is_instance {{.*}} is @__obelisk_class_s10_Constrained
// LOWERED-DAG: obelisk_sim.class.is_instance {{.*}} is @__obelisk_class_s17_Hooked
