// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Leaf", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Leaf", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s3.Leaf", this_variable_path = "Leaf::this", this_variable_symbol = @s1.$root::@s2::@s3.Leaf::@s5.leaf_this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Leaf::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Leaf::limit", name = "limit", node_id = 33 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s29.limit"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "Leaf::positive", name = "positive", node_id = 17 : i64, sym_name = "s17.positive", this_variable_path = "Leaf::positive.this", this_variable_symbol = @s1.$root::@s2::@s3.Leaf::@s17.positive::@s18.constraint_this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 18 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 19 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 20 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "Leaf::x", referenced_symbol = @s1.$root::@s2::@s3.Leaf::@s4.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
                obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 22 : i64, referenced_path = "Leaf::limit", referenced_symbol = @s1.$root::@s2::@s3.Leaf::@s29.limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Leaf::positive.this", is_compiler_generated, is_const, name = "this", node_id = 23 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s18.constraint_this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Leaf::pre_randomize", is_pre_post_randomize, name = "pre_randomize", node_id = 24 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s24.pre_randomize", this_variable_path = "Leaf::pre_randomize.this", this_variable_symbol = @s1.$root::@s2::@s3.Leaf::@s24.pre_randomize::@s25.pre_this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 25 : i64} {
            obelisk.sv.statement.expression_statement attributes {node_id = 30 : i64} {
              obelisk.sv.expression.named_value attributes {node_id = 31 : i64, referenced_path = "top.limit", referenced_symbol = @s1.$root::@s10.top::@s11.top::@s28.limit, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Leaf::pre_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s25.pre_this"} {
          }
        }
        obelisk.sv.symbol.subroutine attributes {hierarchical_name = "Leaf::post_randomize", is_pre_post_randomize, name = "post_randomize", node_id = 27 : i64, semantic_type = !obelisk.subroutine<() -> !obelisk.void, false>, subroutine_kind = 0 : i32, sym_name = "s26.post_randomize", this_variable_path = "Leaf::post_randomize.this", this_variable_symbol = @s1.$root::@s2::@s3.Leaf::@s26.post_randomize::@s27.post_this, time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.list attributes {node_id = 28 : i64} {
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "Leaf::post_randomize.this", is_compiler_generated, is_const, name = "this", node_id = 29 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s27.post_this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Leaf::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s5.leaf_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 96 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Parent", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Parent", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.Parent>, sym_name = "s6.Parent", this_variable_path = "Parent::this", this_variable_symbol = @s1.$root::@s2::@s6.Parent::@s9.parent_this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::y", name = "y", node_id = 7 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::leaf", name = "leaf", node_id = 8 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s8.leaf"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::leaf2", name = "leaf2", node_id = 34 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s30.leaf2"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Parent::this", is_compiler_generated, is_const, name = "this", node_id = 9 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.Parent>, sym_name = "s9.parent_this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.limit", lifetime = 1 : i32, name = "limit", node_id = 32 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s28.limit"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.p", lifetime = 1 : i32, name = "p", node_id = 12 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.Parent>, sym_name = "s12.p"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 13 : i64, procedure_kind = 0 : i32, sym_name = "s13", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 14 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 15 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s10.top::@s11.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 16 : i64, referenced_path = "top.p", referenced_symbol = @s1.$root::@s10.top::@s11.top::@s12.p, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.Parent>} {
              }
            }
          }
        }
      }
    }
  }
}

// A rand handle is never replaced. A null child contributes no mutable random
// fields; a non-null child contributes x to the same aggregate assignment as
// parent.y and is gated by both objects' rand_mode state.
// IEEE 1800-2023 18.6.2 additionally invokes the enabled, non-null child's
// lifecycle hooks. The child pre hook precedes sampling its modes and values;
// the child post hook follows a successful commit and is absent on failure.
// CHECK-DAG: obelisk_sim.func private @[[LEAF_PRE:unit_[0-9]+]]{{.*}}obelisk_sim.hierarchical_name = "Leaf::pre_randomize"
// CHECK-DAG: obelisk_sim.func private @[[LEAF_POST:unit_[0-9]+]]{{.*}}obelisk_sim.hierarchical_name = "Leaf::post_randomize"
// CHECK-LABEL: obelisk_sim.func private @{{unit_[0-9]+}}{{.*}}obelisk_sim.hierarchical_name = "top"
// CHECK: %[[LEAF_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s6_Parent_field_1]
// CHECK: %[[LEAF2_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s6_Parent_field_2]
// CHECK: %[[HOOK_LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]] : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Leaf>
// CHECK: %[[HOOK_NULL:.*]] = obelisk_sim.managed.is_null %[[HOOK_LEAF]]
// CHECK: %[[HOOK_NONNULL:.*]] = arith.xori %[[HOOK_NULL]], {{%true[^ ]*}}
// CHECK: %[[HOOK_ENABLED:.*]] = arith.andi {{.*}}, %[[HOOK_NONNULL]]
// CHECK: cf.cond_br %[[HOOK_ENABLED]], ^[[PRE:bb[0-9]+]], ^[[AFTER_PRE:bb[0-9]+]]
// CHECK: ^[[PRE]]:
// CHECK-NEXT: obelisk_sim.class.direct_call @[[LEAF_PRE]] %[[HOOK_LEAF]]({{.*}})
// CHECK-NEXT: cf.br ^[[AFTER_PRE]]
// CHECK: ^[[AFTER_PRE]]:
// CHECK: %[[MODE_LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]] : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Leaf>
// CHECK: %[[MODE_NULL:.*]] = obelisk_sim.managed.is_null %[[MODE_LEAF]]
// CHECK: cf.cond_br %[[MODE_NULL]]
// CHECK: obelisk_sim.class.field_ref %[[MODE_LEAF]][@__obelisk_class_s3_Leaf_field___obelisk_constraint_mode]
// CHECK: %[[LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]] : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Leaf>
// CHECK: %[[NULL:.*]] = obelisk_sim.managed.is_null %[[LEAF]]
// CHECK: cf.cond_br %[[NULL]], ^[[MERGE:bb[0-9]+]]({{.*}}%false{{.*}}), ^[[OBJECT_BLOCK:bb[0-9]+]]
// CHECK: ^[[OBJECT_BLOCK]]:
// CHECK: %[[X_REF:.*]] = obelisk_sim.class.field_ref %[[LEAF]][@__obelisk_class_s3_Leaf_field_0]
// CHECK: obelisk_sim.managed.load %[[X_REF]]
// CHECK: obelisk_sim.class.field_ref %[[LEAF]][@__obelisk_class_s3_Leaf_field___obelisk_rand_mode]
// CHECK: cf.br ^[[MERGE]]
// CHECK: ^[[MERGE]]({{.*}}, %[[CHILD_ENABLED:.*]]: i1):
// CHECK: arith.andi {{.*}}, %[[CHILD_ENABLED]] : i1
// CHECK: %[[STATE_CHILD:.*]] = obelisk_sim.managed.load %[[LEAF_REF]] : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Leaf>
// CHECK: %[[STATE_NULL:.*]] = obelisk_sim.managed.is_null %[[STATE_CHILD]]
// CHECK: cf.cond_br %[[STATE_NULL]], ^[[STATE_RESUME:bb[0-9]+]]({{.*}} : i32), ^[[STATE_PRESENT:bb[0-9]+]]
// CHECK: ^[[STATE_PRESENT]]:
// CHECK: %[[LIMIT_REF:.*]] = obelisk_sim.class.field_ref %[[STATE_CHILD]][@__obelisk_class_s3_Leaf_field_1]
// CHECK: %[[LIMIT:.*]] = obelisk_sim.managed.load %[[LIMIT_REF]]
// CHECK: cf.br ^[[STATE_RESUME]](%[[LIMIT]] : i32)
// CHECK: ^[[STATE_RESUME]](%[[CAPTURE:.*]]: i32):
// CHECK: %[[EXTENDED_CAPTURE:.*]] = arith.extui %[[CAPTURE]] : i32 to i64
// CHECK: %[[STATE2_CHILD:.*]] = obelisk_sim.managed.load %[[LEAF2_REF]] : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Leaf>
// CHECK: %[[STATE2_NULL:.*]] = obelisk_sim.managed.is_null %[[STATE2_CHILD]]
// CHECK: cf.cond_br %[[STATE2_NULL]], ^[[STATE2_RESUME:bb[0-9]+]]({{.*}} : i32), ^[[STATE2_PRESENT:bb[0-9]+]]
// CHECK: ^[[STATE2_PRESENT]]:
// CHECK: %[[LIMIT2_REF:.*]] = obelisk_sim.class.field_ref %[[STATE2_CHILD]][@__obelisk_class_s3_Leaf_field_1]
// CHECK: %[[LIMIT2:.*]] = obelisk_sim.managed.load %[[LIMIT2_REF]]
// CHECK: cf.br ^[[STATE2_RESUME]](%[[LIMIT2]] : i32)
// CHECK: ^[[STATE2_RESUME]](%[[CAPTURE2:.*]]: i32):
// CHECK: %[[EXTENDED_CAPTURE2:.*]] = arith.extui %[[CAPTURE2]] : i32 to i64
// CHECK: obelisk_sim.random.solve_wide {{.*}}, %[[EXTENDED_CAPTURE]], %[[EXTENDED_CAPTURE2]] {program =
// CHECK: ^[[POST_DISPATCH:bb[0-9]+]]:  // 3 preds:
// CHECK-NEXT: cf.cond_br %[[HOOK_ENABLED]], ^[[POST:bb[0-9]+]], ^[[AFTER_POST:bb[0-9]+]]
// CHECK: %[[COMMIT_X_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s3_Leaf_field_0]
// CHECK: obelisk_sim.managed.store {{.*}} to %[[COMMIT_X_REF]]
// CHECK-NEXT: cf.br ^[[NEXT_CHILD:bb[0-9]+]]
// CHECK: ^[[NEXT_CHILD]]:
// CHECK: cf.cond_br {{.*}}, ^[[STORE_CHILD2:bb[0-9]+]], ^[[POST_DISPATCH]]
// CHECK: ^[[STORE_CHILD2]]:
// CHECK: %[[COMMIT_LEAF2:.*]] = obelisk_sim.managed.load %[[LEAF2_REF]] : {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_Leaf>
// CHECK: %[[COMMIT_X2_REF:.*]] = obelisk_sim.class.field_ref %[[COMMIT_LEAF2]][@__obelisk_class_s3_Leaf_field_0]
// CHECK: obelisk_sim.managed.store {{.*}} to %[[COMMIT_X2_REF]]
// CHECK-NEXT: cf.br ^[[POST_DISPATCH]]
// CHECK: ^[[POST]]:
// CHECK-NEXT: obelisk_sim.class.direct_call @[[LEAF_POST]] %[[HOOK_LEAF]]()
// CHECK-NEXT: cf.br ^[[AFTER_POST]]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[LEAF_REF]]
