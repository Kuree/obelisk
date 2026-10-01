// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s
// RUN: obelisk-opt %s --obelisk-sim-prepare | FileCheck %s --check-prefix=PREPARE

module {
  obelisk.sv.symbol.definition @s0.top attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64} {
  }
  obelisk.sv.symbol.root @s1.$root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64} {
    obelisk.sv.symbol.compilation_unit @s2 attributes {hierarchical_name = "$unit", node_id = 2 : i64} {
      obelisk.sv.symbol.variable @s3.base_seed attributes {hierarchical_name = "base_seed", lifetime = 1 : i32, name = "base_seed", node_id = 3 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 4 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
      obelisk.sv.symbol.variable @s4.field_seed attributes {hierarchical_name = "field_seed", lifetime = 1 : i32, name = "field_seed", node_id = 5 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        obelisk.sv.expression.integer_literal attributes {constant_value = "2", is_declared_unsized = true, is_signed = true, node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
        }
      }
      obelisk.sv.type.enum_type @s30.policy_t attributes {hierarchical_name = "policy_t", name = "policy_t", node_id = 100 : i64, semantic_type = !obelisk.enum<"policy_t", !obelisk.integral<32, true, false, 31 : 0, int>>} {
        obelisk.sv.symbol.enum_value @s31.SEVEN attributes {constant_value = "7", hierarchical_name = "policy_t.SEVEN", name = "SEVEN", node_id = 101 : i64, semantic_type = !obelisk.enum<"policy_t", !obelisk.integral<32, true, false, 31 : 0, int>>} {
          obelisk.sv.expression.integer_literal attributes {constant_value = "7", is_declared_unsized = true, is_signed = true, node_id = 102 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
      }
      obelisk.sv.type.class_type @s5.Base attributes {bitstream_width = 32 : i64, constructor_path = "Base::new", constructor_symbol = @s1.$root::@s2::@s5.Base::@s6.new, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Base", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Base", node_id = 7 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s5.Base>, this_variable_path = "Base::this", this_variable_symbol = @s1.$root::@s2::@s5.Base::@s8.this} {
        obelisk.sv.symbol.class_property @s33.policy attributes {hierarchical_name = "Base::policy", name = "policy", node_id = 105 : i64, semantic_type = !obelisk.enum<"policy_t", !obelisk.integral<32, true, false, 31 : 0, int>>} {
          obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 106 : i64, referenced_path = "policy_t.SEVEN", referenced_symbol = @s1.$root::@s2::@s30.policy_t::@s31.SEVEN, semantic_type = !obelisk.enum<"policy_t", !obelisk.integral<32, true, false, 31 : 0, int>>} {
          }
        }
        obelisk.sv.symbol.subroutine @s6.new attributes {hierarchical_name = "Base::new", is_constructor, name = "new", node_id = 8 : i64, semantic_type = !obelisk.subroutine<(!obelisk.integral<32, true, false, 31 : 0, int>) -> !obelisk.void, false>, subroutine_kind = 0 : i32, this_variable_path = "Base::new.this", this_variable_symbol = @s1.$root::@s2::@s5.Base::@s6.new::@s7.this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.symbol.formal_argument @s17.value attributes {direction = 0 : i32, hierarchical_name = "Base::new.value", name = "value", node_id = 26 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 27 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
            }
          }
          obelisk.sv.statement.expression_statement attributes {node_id = 9 : i64} {
            obelisk.sv.expression.unary_op attributes {is_signed = true, node_id = 10 : i64, operator_kind = 12 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 11 : i64, referenced_path = "base_seed", referenced_symbol = @s1.$root::@s2::@s3.base_seed, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.variable @s7.this attributes {hierarchical_name = "Base::new.this", is_compiler_generated, is_const, name = "this", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s5.Base>} {
          }
        }
        obelisk.sv.symbol.variable @s8.this attributes {hierarchical_name = "Base::this", is_compiler_generated, is_const, name = "this", node_id = 13 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s5.Base>} {
        }
      }
      obelisk.sv.type.class_type @s9.Derived attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s5.Base>, bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Derived", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Derived", node_id = 14 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.Derived>, this_variable_path = "Derived::this", this_variable_symbol = @s1.$root::@s2::@s9.Derived::@s11.this} {
        obelisk.sv.symbol.class_property @s10.value attributes {hierarchical_name = "Derived::value", name = "value", node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 16 : i64, referenced_path = "field_seed", referenced_symbol = @s1.$root::@s2::@s4.field_seed, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
          }
        }
        obelisk.sv.symbol.class_property @s32.policy attributes {hierarchical_name = "Derived::policy", name = "policy", node_id = 103 : i64, semantic_type = !obelisk.enum<"policy_t", !obelisk.integral<32, true, false, 31 : 0, int>>} {
          obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 104 : i64, referenced_path = "policy_t.SEVEN", referenced_symbol = @s1.$root::@s2::@s30.policy_t::@s31.SEVEN, semantic_type = !obelisk.enum<"policy_t", !obelisk.integral<32, true, false, 31 : 0, int>>} {
          }
        }
        obelisk.sv.symbol.variable @s11.this attributes {hierarchical_name = "Derived::this", is_compiler_generated, is_const, name = "this", node_id = 17 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.Derived>} {
        }
      }
    }
    obelisk.sv.symbol.instance @s12.top attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 18 : i64, referenced_path = "top", referenced_symbol = @s0.top} {
      obelisk.sv.symbol.instance_body @s13.top attributes {hierarchical_name = "top", name = "top", node_id = 19 : i64, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.statement_block @s14 attributes {block_kind = 0 : i32, hierarchical_name = "top", node_id = 20 : i64} {
          obelisk.sv.symbol.variable @s15.object attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 21 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.Derived>} {
            obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 22 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s9.Derived>} {
            }
          }
        }
        obelisk.sv.symbol.procedural_block @s16 attributes {hierarchical_name = "top", node_id = 23 : i64, procedure_kind = 0 : i32, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.block attributes {node_id = 24 : i64} {
            obelisk.sv.statement.variable_declaration attributes {node_id = 25 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s12.top::@s13.top::@s14::@s15.object} {
            }
          }
        }
      }
    }
  }
}

// Descriptor-backed constructor state is resolved directly through the
// simulation context instead of being threaded through constructor ABIs.
// PREPARE: simulation.class.decl @[[PREPARED_DERIVED:[^ ]+]] {{.*}}implicit_constructor = @[[PREPARED_NEW:[^, }]+]]
// PREPARE: obelisk.sv.expression.new_class attributes {{.*}}simulation.callee_captures = []
// PREPARE-SAME: simulation.callee_read_captures = ["base_seed", "field_seed"]
// CHECK: simulation.func private @[[BASE_NEW:unit_[0-9]+]](%[[BASE_CONTEXT:arg[0-9]+]]: !simulation.context{{.*}}%{{.*}}: !simulation.class_handle<@[[BASE_CLASS:[^>]+]]>{{.*}}%{{.*}}: i32{{.*}}simulation.hierarchical_name = "Base::new"
// CHECK: %[[BASE_POLICY:.*]] = arith.constant 7 : i32
// CHECK: %[[BASE_SEED:.*]] = simulation.context.storage %[[BASE_CONTEXT]][0] : !simulation.ref<i32>
// CHECK: simulation.managed.store %[[BASE_POLICY]]

// Capture the synthesized constructor symbol at its call site for the
// definition below.
// CHECK: simulation.class.direct_call @[[DERIVED_NEW:[^ ]+]] {{.*}}()

// The synthesized derived constructor resolves its own field initializer from
// context and the base constructor independently resolves base_seed.
// CHECK: simulation.func private @[[DERIVED_NEW]](%[[DERIVED_CONTEXT:arg[0-9]+]]: !simulation.context{{.*}}%[[DERIVED_THIS:arg[0-9]+]]: !simulation.class_handle<@[[DERIVED_CLASS:[^>]+]]>{{.*}}simulation.hierarchical_name = "Derived::new"
// CHECK: %[[DEFAULT:.*]] = arith.constant 3 : i32
// CHECK: %[[POLICY:.*]] = arith.constant 7 : i32
// CHECK: %[[FIELD_SEED:.*]] = simulation.context.storage %[[DERIVED_CONTEXT]][1] : !simulation.ref<i32>
// CHECK: %[[CAST:.*]] = simulation.class.cast %[[DERIVED_THIS]] : !simulation.class_handle<@[[DERIVED_CLASS]]> to !simulation.class_handle<@[[BASE_CLASS]]>
// CHECK-NEXT: simulation.class.direct_call @[[BASE_NEW]] %[[CAST]](%[[DEFAULT]])
// CHECK: %[[FIELD_VALUE:.*]] = simulation.ref.load %[[FIELD_SEED]] : !simulation.ref<i32> -> i32
// CHECK: simulation.managed.store %[[FIELD_VALUE]]
// CHECK: simulation.managed.store %[[POLICY]]
