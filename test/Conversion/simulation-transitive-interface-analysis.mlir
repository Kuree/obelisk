// RUN: obelisk-opt %s --obelisk-sim-devirtualize-class-calls | FileCheck %s --check-prefix=DEVIRT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-sccp))' | FileCheck %s --check-prefix=SCCP
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=GRAPH

module {
  obelisk_sim.design @transitive_interfaces {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "C.get"
    obelisk_sim.code_unit.decl 3 in 0 task hierarchy "C.run"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "caller"

    obelisk_sim.class.decl @I id 1 {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @J id 2 implements [@I] {
      is_abstract = true, is_final = false, is_interface = true
    }
    obelisk_sim.class.decl @C id 3 implements [@J] {
      is_abstract = false, is_final = true, is_interface = false
    }

    obelisk_sim.class.method @I_get of @I slot 4294967295
        signature_id 17 interface_ordinal 0 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>) -> i64 {
        is_final = false, is_pure = true, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @I_run of @I slot 4294967295
        signature_id 18 interface_ordinal 1 :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@I>, i32) -> () {
        is_final = false, is_pure = true, is_static = false,
        is_task = true, is_virtual = true
      }
    obelisk_sim.class.method @C_get of @C slot 0 signature_id 17
        implemented_by @c_get :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>) -> i64 {
        is_final = true, is_pure = false, is_static = false,
        is_task = false, is_virtual = true
      }
    obelisk_sim.class.method @C_run of @C slot 1 signature_id 18
        implemented_by @c_run :
      (!obelisk_sim.context, !obelisk_sim.class_handle<@C>, i32) -> () {
        is_final = true, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }

    obelisk_sim.func private @c_get(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32}) -> i64
        attributes {code_unit_id = 2 : i64, entry_kind = 8 : i32} {
      %value = arith.constant 7 : i64
      obelisk_sim.return %value : i64
    }
    obelisk_sim.func private @c_run(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %this: !obelisk_sim.class_handle<@C>
          {obelisk_sim.capture_kind = 1 : i32},
        %value: i32 {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 12 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      %local = obelisk_sim.ref.alloc %sum : i32 -> !obelisk_sim.ref<i32>
      obelisk_sim.return
    }
    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 0 : i32} {
      %object = obelisk_sim.class.alloc %ctx :
        !obelisk_sim.context -> !obelisk_sim.class_handle<@C>
      %interface = obelisk_sim.class.cast %object :
        !obelisk_sim.class_handle<@C> to !obelisk_sim.class_handle<@I>
      %value = obelisk_sim.class.virtual_call
        %interface[@I_get] slot 4294967295 signature_id 17() :
        (!obelisk_sim.class_handle<@I>) -> i64
      obelisk_sim.return
    }
    obelisk_sim.func @caller(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %receiver: !obelisk_sim.class_handle<@I>
          {obelisk_sim.capture_kind = 1 : i32})
        attributes {code_unit_id = 4 : i64, entry_kind = 1 : i32} {
      %input = arith.constant 41 : i32
      obelisk_sim.class.virtual_task_call
        %receiver[@I_run] slot 4294967295 signature_id 18
        (%input) arguments 1 to ^done :
        (!obelisk_sim.class_handle<@I>, i32) -> ()
    ^done:
      obelisk_sim.return
    }
  }
}

// DEVIRT-LABEL: obelisk_sim.func @root
// DEVIRT: %[[OBJECT:.*]] = obelisk_sim.class.alloc
// DEVIRT: %[[INTERFACE:.*]] = obelisk_sim.class.cast %[[OBJECT]]
// DEVIRT: %[[THIS:.*]] = obelisk_sim.class.cast %[[INTERFACE]]
// DEVIRT-NEXT: obelisk_sim.call @c_get(%{{.*}}, %[[THIS]])
// DEVIRT-LABEL: obelisk_sim.func @caller
// DEVIRT: obelisk_sim.managed.is_null
// DEVIRT: cf.cond_br
// DEVIRT: obelisk_sim.class.virtual_task_call
// DEVIRT: obelisk_sim.task.call @c_run

// SCCP-LABEL: obelisk_sim.func private @c_run
// SCCP: arith.constant 42 : i32
// SCCP-LABEL: obelisk_sim.func @caller
// SCCP: obelisk_sim.class.virtual_task_call

// GRAPH: compute_graph = #obelisk_sim.graph<
// GRAPH-SAME: function = @c_run
// GRAPH-SAME: #obelisk_sim.edge<source = 0, target = 2, kind = process_order>
// GRAPH-SAME: #obelisk_sim.edge<source = 1, target = 0, kind = process_order>
