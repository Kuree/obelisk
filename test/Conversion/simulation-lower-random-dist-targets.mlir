// REQUIRES: compiled-sampler
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | obelisk-opt --test-obelisk-print-random-programs -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 18.5.3 distributes the value of any integral expression that
// contains a rand variable. The constant selection p[3:0] is the assignment
// field at bit 8. The sum a + b (32 bits in its context) has no field: its
// bits are exported after its group's guard, and its records are marked as
// an expression target with offset zero.

// CHECK: simulation.random.solve: {{.*}} flags [HAS_DIST, {{.*}}EXACT_WEIGHTED]
// CHECK: END_DIST_GUARD width 1{{$}}
// CHECK-NEXT: PUSH_VARIABLE width 4{{$}}
// CHECK-NEXT: CAST width 32
// CHECK-NEXT: PUSH_VARIABLE width 4 operand 4{{$}}
// CHECK-NEXT: CAST width 32
// CHECK-NEXT: ADD width 32
// CHECK-NEXT: CAST width 32
// CHECK-NEXT: END_DIST_TARGET_BIT width 32{{$}}
// CHECK-NEXT: END_DIST_TARGET_BIT width 32 auxiliary 1{{$}}
// CHECK: END_DIST_TARGET_BIT width 32 auxiliary 31{{$}}
// CHECK-NEXT: PUSH_LITERAL width 1 value 1
// CHECK-NEXT: END_DIST_GUARD width 1 operand 1{{$}}
// CHECK-NEXT: dist group 0 block 0 offset 0 width 32 soft 0 lower 0 cardinality 1 {{.*}} flags [RAW_WEIGHT, EXPRESSION_TARGET]
// CHECK-NEXT: dist group 0 block 0 offset 0 width 32 soft 0 lower 1 cardinality 15 {{.*}} flags [RAW_WEIGHT, PER_RANGE, EXPRESSION_TARGET]
// CHECK-NEXT: dist group 1 block 0 offset 8 width 4 soft 0 lower 0 cardinality 1 {{.*}} flags [RAW_WEIGHT]
// CHECK-NEXT: dist group 1 block 0 offset 8 width 4 soft 0 lower 1 cardinality 15 {{.*}} flags [RAW_WEIGHT, PER_RANGE]

!u4 = !obelisk.ranged_packed_array<3 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!u8 = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!u32 = !obelisk.ranged_packed_array<31 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!void = !obelisk.void
!c = !obelisk.class_handle<@C>

module {
  simulation.design @dist_targets {
    simulation.class.decl @__obelisk_class_C id 1 {
        debug_name = "C", is_abstract = false, is_final = false,
        is_interface = false,
        random_variable_references = [
          #simulation.random_variable_reference<target = @a>,
          #simulation.random_variable_reference<target = @b>,
          #simulation.random_variable_reference<target = @p>],
        simulation.constraint_mode_field = @constraint_mode,
        simulation.random_increment_field = @rng_increment,
        simulation.random_mode_field = @rand_mode,
        simulation.random_state_field = @rng_state}
    simulation.class.field @a of @__obelisk_class_C at 0 : !simulation.packed_array<3 : 0 x i1> {debug_name = "a", is_static = false, is_weak = false, simulation.random_mode_index = 0 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @b of @__obelisk_class_C at 1 : !simulation.packed_array<3 : 0 x i1> {debug_name = "b", is_static = false, is_weak = false, simulation.random_mode_index = 1 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @p of @__obelisk_class_C at 2 : !simulation.packed_array<7 : 0 x i1> {debug_name = "p", is_static = false, is_weak = false, simulation.random_mode_index = 2 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @rng_state of @__obelisk_class_C at 3 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @rng_increment of @__obelisk_class_C at 4 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @rand_mode of @__obelisk_class_C at 5 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @constraint_mode of @__obelisk_class_C at 6 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.class_handle<@__obelisk_class_C> design hierarchy "top.o"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top"

    simulation.func private @top(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %o: !simulation.ref<!simulation.class_handle<@__obelisk_class_C>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.o", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, is_void_casted, node_id = 2 : i64, semantic_type = !int, simulation.constraint_mode_static_storages = array<i64: -1>, simulation.random_constraint_count = 1 : i32, simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_properties = [{field = @a, is_randc = false, is_signed = false, rand_mode_index = 0 : i32, type = !simulation.packed_array<3 : 0 x i1>, width = 4 : i64}, {field = @b, is_randc = false, is_signed = false, rand_mode_index = 1 : i32, type = !simulation.packed_array<3 : 0 x i1>, width = 4 : i64}, {field = @p, is_randc = false, is_signed = false, rand_mode_index = 2 : i32, type = !simulation.packed_array<7 : 0 x i1>, width = 8 : i64}], simulation.random_receiver_index = 0 : i32, simulation.random_recursive_alias_guards = [], simulation.random_total_width = 16 : i64, simulation.randomize, simulation.randomize_plan_class = @__obelisk_class_C, subroutine_kind = 0 : i32} {
          obelisk.sv.expression.named_value attributes {node_id = 3 : i64, referenced_path = "top.o", referenced_symbol = @o, semantic_type = !c} {
          }
          obelisk.sv.constraint.list attributes {item_count = 2 : i64, node_id = 4 : i64, simulation.random_constraint_block = 0 : i32} {
            // (a + b) dist {0 := 1, [1:15] :/ 1};
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 5 : i64, simulation.random_priority_arguments = 0 : i64, simulation.random_priority_inputs = 255 : i64} {
              obelisk.sv.expression.dist attributes {default_weight_kind = 0 : i64, has_default_weight = false, item_count = 2 : i64, item_has_weight = array<i64: 1, 1>, item_weight_kinds = array<i64: 0, 1>, node_id = 6 : i64, semantic_type = !void} {
                obelisk.sv.expression.binary_op attributes {node_id = 7 : i64, operator_kind = 0 : i32, semantic_type = !u32} {
                  obelisk.sv.expression.conversion attributes {is_implicit = true, node_id = 8 : i64, semantic_type = !u32} {
                    obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::a", referenced_symbol = @a, semantic_type = !u4, simulation.class_field = @a, simulation.random_variable = 0 : i32} {
                    }
                  }
                  obelisk.sv.expression.conversion attributes {is_implicit = true, node_id = 10 : i64, semantic_type = !u32} {
                    obelisk.sv.expression.named_value attributes {node_id = 11 : i64, referenced_path = "C::b", referenced_symbol = @b, semantic_type = !u4, simulation.class_field = @b, simulation.random_variable = 1 : i32} {
                    }
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "32'd0", node_id = 12 : i64, semantic_type = !u32} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 13 : i64, semantic_type = !int} {
                }
                obelisk.sv.expression.value_range attributes {node_id = 14 : i64, range_kind = 0 : i32, semantic_type = !void} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "32'd1", node_id = 15 : i64, semantic_type = !u32} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "32'd15", node_id = 16 : i64, semantic_type = !u32} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 17 : i64, semantic_type = !int} {
                }
              }
            }
            // p[3:0] dist {0 := 1, [1:15] :/ 1};
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 18 : i64, simulation.random_priority_arguments = 0 : i64, simulation.random_priority_inputs = 3840 : i64} {
              obelisk.sv.expression.dist attributes {default_weight_kind = 0 : i64, has_default_weight = false, item_count = 2 : i64, item_has_weight = array<i64: 1, 1>, item_weight_kinds = array<i64: 0, 1>, node_id = 19 : i64, semantic_type = !void} {
                obelisk.sv.expression.conversion attributes {is_implicit = true, node_id = 20 : i64, semantic_type = !u32} {
                  obelisk.sv.expression.range_select attributes {node_id = 21 : i64, selection_kind = 0 : i32, semantic_type = !u4, simulation.random_variable_bit_offset = 8 : i64} {
                    obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "C::p", referenced_symbol = @p, semantic_type = !u8, simulation.class_field = @p, simulation.random_variable = 2 : i32} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "3", is_declared_unsized = true, is_signed = true, node_id = 23 : i64, semantic_type = !int} {
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 24 : i64, semantic_type = !int} {
                    }
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "32'd0", node_id = 25 : i64, semantic_type = !u32} {
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 26 : i64, semantic_type = !int} {
                }
                obelisk.sv.expression.value_range attributes {node_id = 27 : i64, range_kind = 0 : i32, semantic_type = !void} {
                  obelisk.sv.expression.integer_literal attributes {constant_value = "32'd1", node_id = 28 : i64, semantic_type = !u32} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "32'd15", node_id = 29 : i64, semantic_type = !u32} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 30 : i64, semantic_type = !int} {
                }
              }
            }
          }
        }
      }
      simulation.return
    }
  }
}
