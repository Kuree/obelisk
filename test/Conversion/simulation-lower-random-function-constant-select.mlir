// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2023 18.5.11: a constraint function body expands into nested
// conditionals selected by the random candidate. A bit-select with a constant
// in-range address (11.5.1) cannot fail, so the arms it guards carry no
// candidate-conditioned evaluation error and the constraint compiles without
// staged function execution. This is the expansion of
//   function bit [7:0] f(bit [7:0] v);
//     if (v[0]) return 100;
//     if (v[1]) return 50;
//     return 0;
//   endfunction
//   constraint c { r == f(a); }

!byte = !obelisk.ranged_packed_array<7 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!c = !obelisk.class_handle<@C>

module {
  simulation.design @constant_select {
    simulation.class.decl @__obelisk_class_C id 1 {
        debug_name = "C", is_abstract = false, is_final = false,
        is_interface = false,
        random_variable_references = [
          #simulation.random_variable_reference<target = @a>,
          #simulation.random_variable_reference<target = @r>],
        simulation.constraint_mode_field = @constraint_mode,
        simulation.random_increment_field = @rng_increment,
        simulation.random_mode_field = @rand_mode,
        simulation.random_state_field = @rng_state}
    simulation.class.field @a of @__obelisk_class_C at 0 : !simulation.packed_array<7 : 0 x i1> {debug_name = "a", is_static = false, is_weak = false, simulation.random_mode_index = 0 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @r of @__obelisk_class_C at 1 : !simulation.packed_array<7 : 0 x i1> {debug_name = "r", is_static = false, is_weak = false, simulation.random_mode_index = 1 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @rng_state of @__obelisk_class_C at 2 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @rng_increment of @__obelisk_class_C at 3 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @rand_mode of @__obelisk_class_C at 4 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @constraint_mode of @__obelisk_class_C at 5 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.class_handle<@__obelisk_class_C> design hierarchy "top.o"
    simulation.code_unit.decl 1 in 0 initial hierarchy "top"

    // CHECK-LABEL: simulation.func private @top
    // CHECK: simulation.random.solve
    simulation.func private @top(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %o: !simulation.ref<!simulation.class_handle<@__obelisk_class_C>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {code_unit_id = 1 : i64, entry_kind = 1 : i32,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.o", argument = 1, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.expression_statement attributes {node_id = 1 : i64} {
        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, is_void_casted, node_id = 2 : i64, semantic_type = !int, simulation.constraint_mode_static_storages = array<i64: -1>, simulation.random_constraint_count = 1 : i32, simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_properties = [#simulation.random_property<field = @a, isRandC = false, isSigned = false, modeIndex = 0, type = !simulation.packed_array<7 : 0 x i1>, width = 8>, #simulation.random_property<field = @r, isRandC = false, isSigned = false, modeIndex = 1, type = !simulation.packed_array<7 : 0 x i1>, width = 8>], simulation.random_receiver_index = 0 : i32, simulation.random_recursive_alias_guards = [], simulation.random_total_width = 16 : i64, simulation.randomize, simulation.randomize_plan_class = @__obelisk_class_C, subroutine_kind = 0 : i32} {
          obelisk.sv.expression.named_value attributes {node_id = 3 : i64, referenced_path = "top.o", referenced_symbol = @o, semantic_type = !c} {
          }
          obelisk.sv.constraint.list attributes {item_count = 1 : i64, node_id = 4 : i64, simulation.random_constraint_block = 0 : i32, simulation.random_function_order = [255, 65280]} {
            obelisk.sv.constraint.expression attributes {is_soft = false, node_id = 5 : i64, simulation.random_priority_arguments = 255 : i64, simulation.random_priority_inputs = 65535 : i64} {
              // r == f(a)
              obelisk.sv.expression.binary_op attributes {node_id = 6 : i64, operator_kind = 9 : i32, semantic_type = !bit} {
                obelisk.sv.expression.named_value attributes {node_id = 7 : i64, referenced_path = "C::r", referenced_symbol = @r, semantic_type = !byte, simulation.class_field = @r, simulation.random_variable = 1 : i32} {
                }
                obelisk.sv.expression.conversion attributes {is_implicit = true, node_id = 8 : i64, semantic_type = !byte, simulation.random_evaluation_inputs = 1 : i64, simulation.random_guard_inputs = 255 : i64} {
                  obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::a", referenced_symbol = @a, semantic_type = !byte, simulation.class_field = @a, simulation.random_variable = 0 : i32} {
                  }
                  // v[0] ? 100 : (v[1] ? 50 : 0)
                  obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 10 : i64, semantic_type = !byte, simulation.random_function_conditional, simulation.random_function_expression} {
                    obelisk.sv.expression.element_select attributes {node_id = 11 : i64, semantic_type = !bit, simulation.random_function_expression, simulation.random_variable_bit_offset = 0 : i64} {
                      obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::a", referenced_symbol = @a, semantic_type = !byte, simulation.class_field = @a, simulation.random_variable = 0 : i32} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "0", is_declared_unsized = true, is_signed = true, node_id = 12 : i64, semantic_type = !int, simulation.random_function_expression} {
                      }
                    }
                    obelisk.sv.expression.integer_literal attributes {constant_value = "8'd100", node_id = 13 : i64, semantic_type = !byte, simulation.random_function_expression} {
                    }
                    obelisk.sv.expression.conditional_op attributes {condition_count = 1 : i64, condition_pattern_flags = array<i64: 0>, node_id = 14 : i64, semantic_type = !byte, simulation.random_function_conditional, simulation.random_function_expression} {
                      obelisk.sv.expression.element_select attributes {node_id = 15 : i64, semantic_type = !bit, simulation.random_function_expression, simulation.random_variable_bit_offset = 1 : i64} {
                        obelisk.sv.expression.named_value attributes {node_id = 9 : i64, referenced_path = "C::a", referenced_symbol = @a, semantic_type = !byte, simulation.class_field = @a, simulation.random_variable = 0 : i32} {
                        }
                        obelisk.sv.expression.integer_literal attributes {constant_value = "1", is_declared_unsized = true, is_signed = true, node_id = 16 : i64, semantic_type = !int, simulation.random_function_expression} {
                        }
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd50", node_id = 17 : i64, semantic_type = !byte, simulation.random_function_expression} {
                      }
                      obelisk.sv.expression.integer_literal attributes {constant_value = "8'd0", node_id = 18 : i64, semantic_type = !byte, simulation.random_function_expression} {
                      }
                    }
                  }
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
