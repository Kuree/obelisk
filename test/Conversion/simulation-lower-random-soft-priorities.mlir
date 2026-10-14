// RUN: obelisk-opt %s --pass-pipeline='builtin.module(test-obelisk-replicate,obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | obelisk-opt --test-obelisk-print-random-programs -o /dev/null 2>&1 | FileCheck %s

// IEEE 1800-2023 18.5.13.1 places no bound on the number of soft constraints.
// foreach (v[i]) soft v[i] == 1 over 70 bits yields 70 dense priorities in
// declaration order, beyond the former limit of 64.

// CHECK-LABEL: simulation.random.solve_wide: {{.*}} flags [HAS_SOFT,
// CHECK: PUSH_VARIABLE width 1{{$}}
// CHECK: END_SOFT width 1{{$}}
// CHECK-NEXT: PUSH_VARIABLE width 1 operand 1{{$}}
// CHECK: END_SOFT width 1 auxiliary 1{{$}}
// CHECK: PUSH_VARIABLE width 1 operand 64{{$}}
// CHECK: END_SOFT width 1 auxiliary 64{{$}}
// CHECK: PUSH_VARIABLE width 1 operand 69{{$}}
// CHECK: END_SOFT width 1 auxiliary 69{{$}}
// CHECK-NOT: END_SOFT
// CHECK-LABEL: simulation.random.solve_wide

!bit = !obelisk.integral<1, false, false, 0 : 0, bit>
!u70 = !obelisk.ranged_packed_array<69 : 0 x !obelisk.integral<1, false, false, 0 : 0, bit>>
!int = !obelisk.integral<32, true, false, 31 : 0, int>
!c = !obelisk.class_handle<@C>

module {
  simulation.design @soft_priorities {
    simulation.class.decl @__obelisk_class_C id 1 {
        debug_name = "C", is_abstract = false, is_final = false,
        is_interface = false,
        random_variable_references = [
          #simulation.random_variable_reference<target = @v>],
        simulation.constraint_mode_field = @constraint_mode,
        simulation.random_increment_field = @rng_increment,
        simulation.random_mode_field = @rand_mode,
        simulation.random_state_field = @rng_state}
    simulation.class.field @v of @__obelisk_class_C at 0 : !simulation.packed_array<69 : 0 x i1> {debug_name = "v", is_static = false, is_weak = false, simulation.random_mode_index = 0 : i64, simulation.random_variable_kind = 1 : i32, simulation.random_variable_signed = false}
    simulation.class.field @rng_state of @__obelisk_class_C at 1 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @rng_increment of @__obelisk_class_C at 2 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @rand_mode of @__obelisk_class_C at 3 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @constraint_mode of @__obelisk_class_C at 4 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
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
        obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, is_void_casted, node_id = 2 : i64, semantic_type = !int, simulation.constraint_mode_static_storages = array<i64: -1>, simulation.random_constraint_count = 1 : i32, simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_properties = [#simulation.random_property<field = @v, isRandC = false, isSigned = false, modeIndex = 0, type = !simulation.packed_array<69 : 0 x i1>, width = 70>], simulation.random_receiver_index = 0 : i32, simulation.random_recursive_alias_guards = [], simulation.random_total_width = 70 : i64, simulation.randomize, simulation.randomize_plan_class = @__obelisk_class_C, subroutine_kind = 0 : i32} {
          obelisk.sv.expression.named_value attributes {node_id = 3 : i64, referenced_path = "top.o", referenced_symbol = @o, semantic_type = !c} {
          }
          obelisk.sv.constraint.list attributes {item_count = 70 : i64, node_id = 4 : i64, simulation.random_constraint_block = 0 : i32} {
            // soft v[#] == 1
            obelisk.sv.constraint.expression attributes {is_soft = true, node_id = 5 : i64, test.replicate = 70} {
              obelisk.sv.expression.binary_op attributes {node_id = 6 : i64, operator_kind = 9 : i32, semantic_type = !bit} {
                obelisk.sv.expression.element_select attributes {node_id = 7 : i64, semantic_type = !bit, simulation.random_variable_bit_offset = 0 : i64, test.replicate_stride = {simulation.random_variable_bit_offset = 1 : i64}} {
                  obelisk.sv.expression.named_value attributes {node_id = 8 : i64, referenced_path = "C::v", referenced_symbol = @v, semantic_type = !u70, simulation.class_field = @v, simulation.random_variable = 0 : i32} {
                  }
                  obelisk.sv.expression.integer_literal attributes {constant_value = "#", node_id = 9 : i64, semantic_type = !int} {
                  }
                }
                obelisk.sv.expression.integer_literal attributes {constant_value = "1'b1", node_id = 10 : i64, semantic_type = !bit} {
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
