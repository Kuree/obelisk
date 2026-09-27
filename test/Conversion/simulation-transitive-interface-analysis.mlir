// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s --check-prefix=DEVIRT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' | FileCheck %s --check-prefix=SCCP
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=GRAPH

module {
  simulation.design @transitive_interfaces {
    simulation.scope.decl 0 hierarchy "top"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 function hierarchy "C.get"
    simulation.code_unit.decl 3 in 0 task hierarchy "C.run"
    simulation.code_unit.decl 4 in 0 initial hierarchy "caller"

    simulation.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @J id 2 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
    simulation.class.decl @C id 3 implements [@J] {
      is_abstract = false, is_final = true, is_interface = false
    }

    simulation.class.method @I_get of @I slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!simulation.context, !simulation.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @I_run of @I slot 4294967295
        signature_id 18 interface_ordinal 1 :
      (!simulation.context, !simulation.class_handle<@I>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @C_get of @C slot 0 signature_id 17
        implemented_by @c_get :
      (!simulation.context, !simulation.class_handle<@C>) -> i64 {
        is_final = true, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    simulation.class.method @C_run of @C slot 1 signature_id 18
        implemented_by @c_run :
      (!simulation.context, !simulation.class_handle<@C>, i32) -> () {
        is_final = true, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    simulation.func private @c_get(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      simulation.return %value : i64
    }
    simulation.func private @c_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@C>
          {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      %local = simulation.ref.alloc %sum : i32 -> !simulation.ref<i32>
      simulation.return
    }
    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = simulation.class.alloc %ctx :
        !simulation.context -> !simulation.class_handle<@C>
      %interface = simulation.class.cast %object :
        !simulation.class_handle<@C> to !simulation.class_handle<@I>
      %value = simulation.class.virtual_call
        %interface[@I_get] slot 4294967295 signature_id 17() :
        (!simulation.class_handle<@I>) -> i64
      simulation.return
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@I>
          {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 1 : i32} {
      %input = arith.constant 41 : i32
      simulation.class.virtual_task_call
        %receiver[@I_run] slot 4294967295 signature_id 18
        (%input) arguments 1 to ^done :
        (!simulation.class_handle<@I>, i32) -> ()
    ^done:
      simulation.return
    }
  }
}

// DEVIRT-LABEL: simulation.func @root
// DEVIRT: %[[OBJECT:.*]] = simulation.class.alloc
// DEVIRT: %[[INTERFACE:.*]] = simulation.class.cast %[[OBJECT]]
// DEVIRT: %[[THIS:.*]] = simulation.class.cast %[[INTERFACE]]
// DEVIRT-NEXT: simulation.call @c_get(%{{.*}}, %[[THIS]])
// DEVIRT-LABEL: simulation.func @caller
// DEVIRT: simulation.managed.is_null
// DEVIRT: cf.cond_br
// DEVIRT: simulation.class.virtual_task_call
// DEVIRT: simulation.task.call @c_run

// SCCP-LABEL: simulation.func private @c_run
// SCCP: arith.constant 42 : i32
// SCCP-LABEL: simulation.func @caller
// SCCP: simulation.class.virtual_task_call

// GRAPH: compute_graph = #schedule.graph<
// GRAPH-SAME: function = @c_run
// GRAPH-SAME: #schedule.edge<source = 0, target = 2, kind = process_order>
// GRAPH-SAME: #schedule.edge<source = 1, target = 0, kind = process_order>
