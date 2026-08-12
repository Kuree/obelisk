// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Leaf", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Leaf", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s3.Leaf", this_variable_path = "Leaf::this", this_variable_symbol = @s1.$root::@s2::@s3.Leaf::@s5.leaf_this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Leaf::x", name = "x", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Leaf::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s5.leaf_this"} {
        }
      }
      obelisk.sv.type.class_type attributes {bitstream_width = 64 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "Parent", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "Parent", node_id = 6 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.Parent>, sym_name = "s6.Parent", this_variable_path = "Parent::this", this_variable_symbol = @s1.$root::@s2::@s6.Parent::@s9.parent_this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::y", name = "y", node_id = 7 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s7.y"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "Parent::leaf", name = "leaf", node_id = 8 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.Leaf>, sym_name = "s8.leaf"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "Parent::this", is_compiler_generated, is_const, name = "this", node_id = 9 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s6.Parent>, sym_name = "s9.parent_this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 10 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s10.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 11 : i64, sym_name = "s11.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
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
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[LEAF_REF:.*]] = obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s6_Parent_field_1]
// CHECK: %[[LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]]
// CHECK: %[[NULL:.*]] = obelisk_sim.managed.is_null %[[LEAF]]
// CHECK: cf.cond_br %[[NULL]], ^[[MERGE:bb[0-9]+]]({{.*}}%false{{.*}}), ^[[OBJECT_BLOCK:bb[0-9]+]]
// CHECK: ^[[OBJECT_BLOCK]]:
// CHECK: %[[X_REF:.*]] = obelisk_sim.class.field_ref %[[LEAF]][@__obelisk_class_s3_Leaf_field_0]
// CHECK: obelisk_sim.managed.load %[[X_REF]]
// CHECK: obelisk_sim.class.field_ref %[[LEAF]][@__obelisk_class_s3_Leaf_field___obelisk_rand_mode]
// CHECK: cf.br ^[[MERGE]]
// CHECK: ^[[MERGE]]({{.*}}, %[[CHILD_ENABLED:.*]]: i1):
// CHECK: arith.andi {{.*}}, %[[CHILD_ENABLED]] : i1
// CHECK: %[[COMMIT_LEAF:.*]] = obelisk_sim.managed.load %[[LEAF_REF]]
// CHECK: %[[COMMIT_X_REF:.*]] = obelisk_sim.class.field_ref %[[COMMIT_LEAF]][@__obelisk_class_s3_Leaf_field_0]
// CHECK: obelisk_sim.managed.store {{.*}} to %[[COMMIT_X_REF]]
// CHECK-NOT: obelisk_sim.managed.store {{.*}} to %[[LEAF_REF]]
