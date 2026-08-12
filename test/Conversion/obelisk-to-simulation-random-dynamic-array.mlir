// RUN: obelisk-opt %s '--lower-obelisk-to-sim=opt-level=0' | FileCheck %s

module {
  obelisk.sv.symbol.definition attributes {definition_kind = 0 : i32, hierarchical_name = "top", name = "top", node_id = 0 : i64, sym_name = "s0.top"} {
  }
  obelisk.sv.symbol.root attributes {hierarchical_name = "\\$root ", name = "$root", node_id = 1 : i64, sym_name = "s1.$root"} {
    obelisk.sv.symbol.compilation_unit attributes {hierarchical_name = "$unit", node_id = 2 : i64, sym_name = "s2"} {
      obelisk.sv.type.class_type attributes {bitstream_width = 0 : i64, declared_interfaces = [], generic_parameter_paths = [], generic_parameter_symbols = [], has_base_constructor_call = false, has_cycles = false, hierarchical_name = "C", implemented_interfaces = [], is_abstract = false, is_final = false, is_interface = false, is_uninstantiated = false, name = "C", node_id = 3 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s3.C", this_variable_path = "C::this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s5.this} {
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::data", name = "data", node_id = 4 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>, sym_name = "s4.data"} {
        }
        obelisk.sv.symbol.class_property attributes {hierarchical_name = "C::items", name = "items", node_id = 13 : i64, rand_mode = 1 : i32, semantic_type = !obelisk.queue<!obelisk.integral<32, true, false, 31 : 0, int>, 0>, sym_name = "s13.items"} {
        }
        obelisk.sv.symbol.constraint_block attributes {hierarchical_name = "C::sized", name = "sized", node_id = 14 : i64, sym_name = "s14.sized", this_variable_path = "C::sized.this", this_variable_symbol = @s1.$root::@s2::@s3.C::@s14.sized::@s20.constraint_this} {
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 15 : i64} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 16 : i64} {
              obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 17 : i64, operator_kind = 19 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 18 : i64, operator_kind = 14 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 19 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 20 : i64, referenced_path = "C::data", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.data, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 21 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
                obelisk.sv.expression.binary_op attributes {is_signed = false, node_id = 22 : i64, operator_kind = 16 : i32, semantic_type = !obelisk.integral<1, false, false, 0 : 0, bit>} {
                  obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "size", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 23 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32} {
                    obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 24 : i64, referenced_path = "C::data", referenced_symbol = @s1.$root::@s2::@s3.C::@s4.data, semantic_type = !obelisk.dynarray<!obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>>} {
                    }
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "1000", is_declared_unsized = true, is_signed = true, node_id = 25 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>} {
                  }
                }
              }
            }
          }
          obelisk.sv.symbol.variable attributes {hierarchical_name = "C::sized.this", is_compiler_generated, is_const, name = "this", node_id = 26 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s20.constraint_this"} {
          }
        }
        obelisk.sv.symbol.variable attributes {hierarchical_name = "C::this", is_compiler_generated, is_const, name = "this", node_id = 5 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s5.this"} {
        }
      }
    }
    obelisk.sv.symbol.instance attributes {hierarchical_name = "top", is_uninstantiated = false, name = "top", node_id = 6 : i64, referenced_path = "top", referenced_symbol = @s0.top, sym_name = "s6.top"} {
      obelisk.sv.symbol.instance_body attributes {hierarchical_name = "top", name = "top", node_id = 7 : i64, sym_name = "s7.top", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
        obelisk.sv.symbol.variable attributes {hierarchical_name = "top.c", lifetime = 1 : i32, name = "c", node_id = 8 : i64, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>, sym_name = "s8.c"} {
        }
        obelisk.sv.symbol.procedural_block attributes {hierarchical_name = "top", node_id = 9 : i64, procedure_kind = 0 : i32, sym_name = "s9", time_precision_fs = 1000000 : i64, time_unit_fs = 1000000 : i64} {
          obelisk.sv.statement.expression_statement attributes {node_id = 10 : i64} {
            obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_signed = true, is_super_class = false, is_system_call = true, node_id = 11 : i64, semantic_type = !obelisk.integral<32, true, false, 31 : 0, int>, subroutine_kind = 0 : i32, system_library_cell = "work.top", system_scope_path = "top", system_scope_symbol = @s1.$root::@s6.top::@s7.top} {
              obelisk.sv.expression.named_value attributes {is_signed = false, node_id = 12 : i64, referenced_path = "top.c", referenced_symbol = @s1.$root::@s6.top::@s7.top::@s8.c, semantic_type = !obelisk.class_handle<@s1.$root::@s2::@s3.C>} {
              }
            }
          }
        }
      }
    }
  }
}

// A hard size constraint participates in the same aggregate solve. The
// accepted size creates the replacement array before its elements are
// randomized. The unconstrained queue retains its size and is not recreated.
// CHECK-LABEL: obelisk_sim.func private @unit_0
// CHECK: %[[ARRAY:.*]] = obelisk_sim.managed.load {{.*}} -> !obelisk_sim.dynamic_array<!obelisk_sim.packed_array<7 : 0 x i1>>
// CHECK: %[[OLD_SIZE:.*]] = obelisk_sim.container.size %[[ARRAY]]
// CHECK: arith.cmpi sgt, {{.*}}, {{.*}} : i32
// CHECK: arith.cmpi slt, {{.*}}, {{.*}} : i32
// CHECK: %[[RESIZE_SOURCE:.*]] = obelisk_sim.managed.load {{.*}} -> !obelisk_sim.dynamic_array<!obelisk_sim.packed_array<7 : 0 x i1>>
// CHECK: %[[RESIZED:.*]] = obelisk_sim.container.create_like %[[RESIZE_SOURCE]], %[[RESIZE_SOURCE]],
// CHECK: obelisk_sim.managed.store %[[RESIZED]]
// CHECK: %[[NEW_ARRAY:.*]] = obelisk_sim.managed.load {{.*}} -> !obelisk_sim.dynamic_array<!obelisk_sim.packed_array<7 : 0 x i1>>
// CHECK: %[[SIZE:.*]] = obelisk_sim.container.size %[[NEW_ARRAY]]
// CHECK: cf.br ^[[HEADER:bb[0-9]+]](
// CHECK: ^[[HEADER]](%[[INDEX:.*]]: i64, %[[STATE:.*]]: i64):
// CHECK: arith.cmpi ult, %[[INDEX]], %[[SIZE]]
// CHECK: obelisk_sim.container.write %[[NEW_ARRAY]], %[[INDEX]],
// CHECK: %[[QUEUE:.*]] = obelisk_sim.managed.load {{.*}} -> !obelisk_sim.queue<i32, 0>
// CHECK: %[[QUEUE_SIZE:.*]] = obelisk_sim.container.size %[[QUEUE]]
// CHECK: arith.cmpi ult, {{.*}}, %[[QUEUE_SIZE]]
// CHECK: obelisk_sim.container.write %[[QUEUE]],
// CHECK-NOT: obelisk_sim.container.create_like %[[QUEUE]]
