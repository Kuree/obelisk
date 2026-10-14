// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk-sim-prepare-unit-lowering,simulation.design(simulation.func(obelisk-sim-lower-unit)))' | FileCheck %s

// IEEE 1800-2023 20.2 and 20.10: $finish, $stop and $fatal end the
// simulation, so no statement after a call that latched termination may run.
// Unit lowering polls the latch after every class constructor, method and
// randomize hook call, and after the implicit base-constructor call that
// preparation places at a constructor's entry, returning when it is set.

!base = !obelisk.class_handle<@Base>
!derived = !obelisk.class_handle<@Derived>
!void = !obelisk.void
!int = !obelisk.integral<32, true, false, 31 : 0, int>

module {
  simulation.design @termination_polls {
    simulation.class.decl @__obelisk_class_Base id 1 {
        debug_name = "Base", implicit_constructor = @base_new,
        is_abstract = false, is_final = false, is_interface = false,
        simulation.constraint_mode_field = @constraint_mode,
        simulation.random_increment_field = @rng_increment,
        simulation.random_mode_field = @rand_mode,
        simulation.random_state_field = @rng_state}
    simulation.class.field @rng_state of @__obelisk_class_Base at 0 : i64 {debug_name = "__obelisk_rng_state", is_static = false, is_weak = false}
    simulation.class.field @rng_increment of @__obelisk_class_Base at 1 : i64 {debug_name = "__obelisk_rng_increment", is_static = false, is_weak = false}
    simulation.class.field @rand_mode of @__obelisk_class_Base at 2 : i64 {debug_name = "__obelisk_rand_mode", is_static = false, is_weak = false}
    simulation.class.field @constraint_mode of @__obelisk_class_Base at 3 : i64 {debug_name = "__obelisk_constraint_mode", is_static = false, is_weak = false}
    simulation.class.decl @__obelisk_class_Derived id 2 extends @__obelisk_class_Base {
        debug_name = "Derived", is_abstract = false, is_final = false,
        is_interface = false}
    simulation.class.method @Base_hook of @__obelisk_class_Base slot 0 signature_id 7 implemented_by @hook : (!simulation.context, !simulation.class_handle<@__obelisk_class_Base>) -> () {debug_name = "hook", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = true}
    simulation.class.method @Derived_direct of @__obelisk_class_Derived implemented_by @direct : (!simulation.context, !simulation.class_handle<@__obelisk_class_Derived>) -> () {debug_name = "direct", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = false, sym_visibility = "private"}
    simulation.class.method @Derived_new of @__obelisk_class_Derived implemented_by @derived_new : (!simulation.context, !simulation.class_handle<@__obelisk_class_Derived>) -> () {debug_name = "new", is_final = false, is_pure = false, is_static = false, is_task = false, is_virtual = false, sym_visibility = "private"}
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.class_handle<@__obelisk_class_Derived> design hierarchy "top.d"
    simulation.storage.decl 1 in 0 : !simulation.class_handle<@__obelisk_class_Base> design hierarchy "top.b"
    simulation.code_unit.decl 1 in 0 function hierarchy "Base::new"
    simulation.code_unit.decl 2 in 0 function hierarchy "Base::hook"
    simulation.code_unit.decl 3 in 0 function hierarchy "Base::pre_randomize"
    simulation.code_unit.decl 4 in 0 function hierarchy "Derived::new"
    simulation.code_unit.decl 5 in 0 function hierarchy "Derived::direct"
    simulation.code_unit.decl 6 in 0 initial hierarchy "top"

    simulation.func private @base_new(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@__obelisk_class_Base> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 8 : i32,
                    simulation.bindings = [], simulation.constructor,
                    simulation.this_argument = 1 : i32} {
      simulation.return
    }
    simulation.func private @hook(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@__obelisk_class_Base> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32,
                    simulation.bindings = [], simulation.this_argument = 1 : i32,
                    simulation.void_function} {
      simulation.return
    }
    simulation.func private @pre_randomize(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@__obelisk_class_Base> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 8 : i32,
                    simulation.bindings = [], simulation.this_argument = 1 : i32,
                    simulation.void_function} {
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @derived_new
    // CHECK: simulation.call @base_new(
    // CHECK-NEXT: %[[BASE:.+]] = simulation.termination.requested
    // CHECK-NEXT: cf.cond_br %[[BASE]], ^[[BASE_STOP:bb[0-9]+]], ^[[BASE_GO:bb[0-9]+]]
    // CHECK: ^[[BASE_STOP]]:
    // CHECK-NEXT: simulation.return
    // CHECK: ^[[BASE_GO]]:
    simulation.func private @derived_new(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@__obelisk_class_Derived> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 8 : i32,
                    simulation.bindings = [], simulation.constructor,
                    simulation.this_argument = 1 : i32} {
      %base = simulation.class.cast %this : !simulation.class_handle<@__obelisk_class_Derived> to !simulation.class_handle<@__obelisk_class_Base>
      simulation.call @base_new(%ctx, %base) : (!simulation.context, !simulation.class_handle<@__obelisk_class_Base>) -> ()
      obelisk.sv.statement.list attributes {node_id = 1 : i64} {
      }
      simulation.return
    }
    simulation.func private @direct(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@__obelisk_class_Derived> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 5 : i64, entry_kind = 8 : i32,
                    simulation.bindings = [], simulation.this_argument = 1 : i32,
                    simulation.void_function} {
      simulation.return
    }

    // CHECK-LABEL: simulation.func private @top
    // CHECK: simulation.class.direct_call @derived_new
    // CHECK-NEXT: %[[NEW:.+]] = simulation.termination.requested
    // CHECK-NEXT: cf.cond_br %[[NEW]], ^[[NEW_STOP:bb[0-9]+]], ^[[NEW_GO:bb[0-9]+]]
    // CHECK: ^[[NEW_STOP]]:
    // CHECK-NEXT: simulation.return
    // CHECK: ^[[NEW_GO]]:
    // CHECK-NEXT: simulation.ref.store
    // CHECK: simulation.class.direct_call @base_new
    // CHECK-NEXT: %[[IMPLICIT:.+]] = simulation.termination.requested
    // CHECK-NEXT: cf.cond_br %[[IMPLICIT]],
    // CHECK: simulation.class.direct_call @direct
    // CHECK-NEXT: %[[DIRECT:.+]] = simulation.termination.requested
    // CHECK-NEXT: cf.cond_br %[[DIRECT]],
    // CHECK: simulation.class.virtual_call %{{.+}}[@Base_hook]
    // CHECK-NEXT: %[[VIRTUAL:.+]] = simulation.termination.requested
    // CHECK-NEXT: cf.cond_br %[[VIRTUAL]],
    // CHECK: simulation.class.direct_call @pre_randomize
    // CHECK-NEXT: %[[HOOK:.+]] = simulation.termination.requested
    // CHECK-NEXT: cf.cond_br %[[HOOK]], ^[[HOOK_STOP:bb[0-9]+]], ^[[HOOK_GO:bb[0-9]+]]
    // CHECK: ^[[HOOK_STOP]]:
    // CHECK-NEXT: simulation.return
    // CHECK: ^[[HOOK_GO]]:
    // CHECK-NEXT: simulation.class.field_ref %{{.+}}[@rng_state]
    simulation.func private @top(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %d: !simulation.ref<!simulation.class_handle<@__obelisk_class_Derived>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %b: !simulation.ref<!simulation.class_handle<@__obelisk_class_Base>>
            {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {code_unit_id = 6 : i64, entry_kind = 1 : i32,
                    simulation.bindings = [
                      #simulation.argument_binding<path = "top.d", argument = 1, kind = direct, copyOut = false>,
                      #simulation.argument_binding<path = "top.b", argument = 2, kind = direct, copyOut = false>]} {
      obelisk.sv.statement.list attributes {node_id = 10 : i64} {
        // d = new;  (explicit constructor)
        obelisk.sv.statement.expression_statement attributes {node_id = 11 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 12 : i64, semantic_type = !derived} {
            obelisk.sv.expression.named_value attributes {node_id = 13 : i64, referenced_path = "top.d", referenced_symbol = @d, semantic_type = !derived} {
            }
            obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 14 : i64, semantic_type = !derived, simulation.callee_captures = [], simulation.callee_read_captures = []} {
              obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "new", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = false, node_id = 15 : i64, referenced_path = "Derived::new", referenced_symbol = @Derived::@new, semantic_type = !void, simulation.callee = @derived_new, simulation.callee_captures = [], simulation.callee_formals = [], simulation.callee_read_captures = [], simulation.callee_written_captures = [], simulation.class_instance, simulation.class_method = @Derived_new, subroutine_kind = 0 : i32} {
              }
            }
          }
        }
        // b = new;  (implicit constructor)
        obelisk.sv.statement.expression_statement attributes {node_id = 16 : i64} {
          obelisk.sv.expression.assignment attributes {assignment_kind = 0 : i32, node_id = 17 : i64, semantic_type = !base} {
            obelisk.sv.expression.named_value attributes {node_id = 18 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !base} {
            }
            obelisk.sv.expression.new_class attributes {is_super_class = false, node_id = 19 : i64, semantic_type = !base, simulation.callee_captures = [], simulation.callee_read_captures = []} {
            }
          }
        }
        // d.direct();
        obelisk.sv.statement.expression_statement attributes {node_id = 20 : i64} {
          obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "direct", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 21 : i64, referenced_path = "Derived::direct", referenced_symbol = @Derived::@direct, semantic_type = !void, simulation.callee = @direct, simulation.callee_captures = [], simulation.callee_formals = [], simulation.callee_read_captures = [], simulation.callee_written_captures = [], simulation.class_instance, simulation.class_method = @Derived_direct, subroutine_kind = 0 : i32} {
            obelisk.sv.expression.named_value attributes {node_id = 22 : i64, referenced_path = "top.d", referenced_symbol = @d, semantic_type = !derived} {
            }
          }
        }
        // b.hook();  (virtual)
        obelisk.sv.statement.expression_statement attributes {node_id = 23 : i64} {
          obelisk.sv.expression.call attributes {argument_count = 0 : i64, callee_name = "hook", constraint_restrictions = [], defaulted_arguments = array<i64>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = true, is_super_class = false, is_system_call = false, node_id = 24 : i64, referenced_path = "Base::hook", referenced_symbol = @Base::@hook, semantic_type = !void, simulation.callee = @hook, simulation.callee_captures = [], simulation.callee_formals = [], simulation.callee_read_captures = [], simulation.callee_written_captures = [], simulation.class_instance, simulation.class_method = @Base_hook, simulation.class_signature = 7 : i64, simulation.class_slot = 0 : i64, simulation.class_virtual, subroutine_kind = 0 : i32} {
            obelisk.sv.expression.named_value attributes {node_id = 25 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !base} {
            }
          }
        }
        // void'(b.randomize());  (pre_randomize hook)
        obelisk.sv.statement.expression_statement attributes {node_id = 26 : i64} {
          obelisk.sv.expression.call attributes {argument_count = 1 : i64, callee_name = "randomize", constraint_restrictions = [], defaulted_arguments = array<i64: 0>, has_inline_constraints = false, has_iterator_expression = false, has_output_arguments = false, has_this_class = false, is_super_class = false, is_system_call = true, is_void_casted, node_id = 27 : i64, semantic_type = !int, simulation.constraint_mode_static_storages = array<i64>, simulation.random_constraint_count = 0 : i32, simulation.random_container_properties = [], simulation.random_nested_constraint_modes = [], simulation.random_nested_hooks = [], simulation.random_pre_hook = #simulation.randomize_hook<callee = @pre_randomize, owner = @__obelisk_class_Base, source = @pre_randomize, captures = [], reads = []>, simulation.random_properties = [], simulation.random_receiver_index = 0 : i32, simulation.random_recursive_alias_guards = [], simulation.random_total_width = 0 : i64, simulation.randomize, simulation.randomize_plan_class = @__obelisk_class_Base, subroutine_kind = 0 : i32} {
            obelisk.sv.expression.named_value attributes {node_id = 28 : i64, referenced_path = "top.b", referenced_symbol = @b, semantic_type = !base} {
            }
          }
        }
      }
      simulation.return
    }
  }
}
