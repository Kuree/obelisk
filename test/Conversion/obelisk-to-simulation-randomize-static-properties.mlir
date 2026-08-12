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
      obelisk.sv.type.class_type attributes {bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s7.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::s", lifetime = 1 : i32, name = "s", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s4.s"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::cycle", lifetime = 1 : i32, name = "cycle", node_id = 5 : i64, rand_mode = 2 : i32, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>, sym_name = "s5.cycle"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::x", name = "x", node_id = 6 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, sym_name = "s6.x"} {
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 7 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s7.this"} {
        }
      }
      obelisk.sv.type.class_type attributes {base_class = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, bitstream_width = 32 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "D", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "D", node_id = 24 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s24.D>, sym_name = "s24.D", this_variable_path = "D::this", this_variable_symbol = @s1.$root::@s2::@s24.D::@s25.this} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "D::this", is_compiler_generated, is_const, name = "this", node_id = 25 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s24.D>, sym_name = "s25.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 8 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s8.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 9 : i64, sym_name = "s9.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.object", lifetime = 1 : i32, name = "object", node_id = 10 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s10.object"} {
          obelisk.sv.expression.new_class attributes {is_signed = false, is_super_class = false, node_id = 11 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
          }
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 12 : i64, procedure_kind = 0 : i32, sym_name = "s11", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 13 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 4 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0, 0, 0, 0>, has_inline_constraints = true, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 14 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s8.top::@s9.top} {
              obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 15 : i64} {
                obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 16 : i64} {
                  obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 17 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                    obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 18 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.s, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                    }
                  }
                }
              }
              obelisk.sv.expression.named_value attributes {node_id = 20 : i64, referenced_path = "top.object", referenced_symbol = @s1.$root::@s8.top::@s9.top::@s10.object, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 21 : i64, referenced_path = "C::s", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.s, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
              obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "C::cycle", referenced_symbol = @s1.$root::@s2::@s3.C::@s5.cycle, semantic_type = !obelisk.ranged_packed_array<1 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>} {
              }
              obelisk.sv.expression.named_value attributes {is_signed = true, node_id = 23 : i64, referenced_path = "C::x", referenced_symbol = @s1.$root::@s2::@s3.C::@s6.x, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
              }
            }
          }
        }
      }
    }
  }
}

// Static selections use class-wide design storage. A static randc property
// also receives one class-wide key and position, so every object shares its
// cycle. All four words are committed only on the successful solve edge.

// CHECK: obelisk_sim.storage.decl 0 in 0 : i32 design hierarchy "C::s" debug "s"
// CHECK: obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.packed_array<1 : 0 x i1> design hierarchy "C::cycle" debug "cycle"
// CHECK: obelisk_sim.storage.decl 3 in 0 : i64 design hierarchy "C::s.$rand_mode" debug "__obelisk_rand_mode"
// CHECK: obelisk_sim.storage.decl 4 in 0 : i64 design hierarchy "C::cycle.$rand_mode" debug "__obelisk_rand_mode"
// CHECK: obelisk_sim.storage.decl 5 in 0 : i64 design hierarchy "C::cycle.$randc_key" debug "__obelisk_static_randc_key"
// CHECK: obelisk_sim.storage.decl 6 in 0 : i64 design hierarchy "C::cycle.$randc_position" debug "__obelisk_static_randc_position"
// CHECK-LABEL: obelisk_sim.func private @unit_1
// CHECK-SAME: %[[S_REF:arg[0-9]+]]: !obelisk_sim.ref<i32>
// CHECK-SAME: %[[CYCLE_REF:arg[0-9]+]]: !obelisk_sim.ref<!obelisk_sim.packed_array<1 : 0 x i1>>
// CHECK-SAME: %[[OBJECT_REF:arg[0-9]+]]: !obelisk_sim.ref<!obelisk_sim.class_handle<@__obelisk_class_s3_C>>
// CHECK-SAME: %[[KEY_REF:arg[0-9]+]]: !obelisk_sim.ref<i64>
// CHECK-SAME: %[[POSITION_REF:arg[0-9]+]]: !obelisk_sim.ref<i64>
// CHECK: %[[OBJECT:.*]] = obelisk_sim.ref.load %[[OBJECT_REF]] {{.*}} -> !obelisk_sim.class_handle<@__obelisk_class_s3_C>
// CHECK: %[[IS_D:.*]] = obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s24_D
// CHECK: cf.cond_br %[[IS_D]]
// CHECK: %[[DERIVED:.*]] = obelisk_sim.class.cast %[[OBJECT]] {{.*}} to !obelisk_sim.class_handle<@__obelisk_class_s24_D>
// CHECK: %[[D_X_REF:.*]] = obelisk_sim.class.field_ref %[[DERIVED]][@__obelisk_class_s3_C_field_2]
// CHECK-NOT: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s3_C_field_0]
// CHECK-NOT: obelisk_sim.class.field_ref {{.*}}[@__obelisk_class_s3_C_field_1]
// CHECK: obelisk_sim.ref.load %[[S_REF]] : !obelisk_sim.ref<i32>
// CHECK: obelisk_sim.ref.load %[[KEY_REF]] : !obelisk_sim.ref<i64>
// CHECK: obelisk_sim.ref.load %[[POSITION_REF]] : !obelisk_sim.ref<i64>
// CHECK: %[[IS_C:.*]] = obelisk_sim.class.is_instance %[[OBJECT]] is @__obelisk_class_s3_C
// CHECK: cf.cond_br %[[IS_C]]
// CHECK: arith.cmpi sgt, {{.*}}, {{.*}} : i32
// CHECK: cf.cond_br %{{.*}}, ^[[D_SUCCESS:bb[0-9]+]]
// CHECK: ^[[D_SUCCESS]]
// CHECK: obelisk_sim.ref.store {{.*}} to %[[S_REF]] : i32, !obelisk_sim.ref<i32>
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[CYCLE_REF]] : !obelisk_sim.packed_array<1 : 0 x i1>, !obelisk_sim.ref<!obelisk_sim.packed_array<1 : 0 x i1>>
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[KEY_REF]] : i64, !obelisk_sim.ref<i64>
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[POSITION_REF]] : i64, !obelisk_sim.ref<i64>
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[D_X_REF]]
// CHECK: %[[C_X_REF:.*]] = obelisk_sim.class.field_ref %[[OBJECT]][@__obelisk_class_s3_C_field_2]
// CHECK: obelisk_sim.ref.load %[[S_REF]] : !obelisk_sim.ref<i32>
// CHECK: obelisk_sim.ref.load %[[KEY_REF]] : !obelisk_sim.ref<i64>
// CHECK: obelisk_sim.ref.load %[[POSITION_REF]] : !obelisk_sim.ref<i64>
// CHECK: cf.cond_br %{{.*}}, ^[[C_SUCCESS:bb[0-9]+]]
// CHECK: ^[[C_SUCCESS]]
// CHECK: obelisk_sim.ref.store {{.*}} to %[[S_REF]] : i32, !obelisk_sim.ref<i32>
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[CYCLE_REF]] : !obelisk_sim.packed_array<1 : 0 x i1>, !obelisk_sim.ref<!obelisk_sim.packed_array<1 : 0 x i1>>
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[KEY_REF]] : i64, !obelisk_sim.ref<i64>
// CHECK-NEXT: obelisk_sim.ref.store {{.*}} to %[[POSITION_REF]] : i64, !obelisk_sim.ref<i64>
// CHECK-NEXT: obelisk_sim.managed.store {{.*}} to %[[C_X_REF]]
