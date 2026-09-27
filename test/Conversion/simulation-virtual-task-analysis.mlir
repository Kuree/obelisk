// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' > %t.threaded
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(obelisk-sim-sccp))' > %t.serial
// RUN: diff -u %t.serial %t.threaded
// RUN: FileCheck %s --check-prefix=SCCP < %t.threaded
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s --check-prefix=GRAPH
// RUN: obelisk-opt %s -o /dev/null --pass-pipeline='builtin.module(test-obelisk-sim-state-domain)' 2>&1 | FileCheck %s --check-prefix=DOMAIN

// SCCP-LABEL: simulation.func private @base_run
// SCCP: arith.constant 42 : i32
// SCCP-LABEL: simulation.func private @derived_run
// SCCP: arith.constant 42 : i32
// SCCP: simulation.class.virtual_task_call

// GRAPH: compute_graph = #schedule.graph<
// GRAPH-SAME: function = @base_run
// GRAPH-SAME: function = @caller
// GRAPH-SAME: function = @derived_run
// GRAPH-SAME: #schedule.edge<source = 0, target = 2, kind = process_order>
// GRAPH-SAME: #schedule.edge<source = 1, target = 0, kind = process_order>
// GRAPH-SAME: #schedule.edge<source = 1, target = 3, kind = process_order>
// GRAPH-SAME: #schedule.edge<source = 3, target = 2, kind = process_order>

// DOMAIN-LABEL: state-domain @virtual_analysis
// DOMAIN-LABEL: func @base_run
// DOMAIN-NEXT: bb0.arg3: two-state (call-actual)
// DOMAIN-NEXT: bb0.op3.result0: two-state (logic-unary)
// DOMAIN-LABEL: func @derived_run
// DOMAIN-NEXT: bb0.arg3: two-state (call-actual)
// DOMAIN-NEXT: bb0.op3.result0: two-state (logic-unary)

module {
  simulation.design @virtual_analysis {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 task hierarchy "Base.run"
    simulation.code_unit.decl 2 in 0 task hierarchy "Derived.run"
    simulation.code_unit.decl 3 in 0 initial hierarchy "caller"
    simulation.class.decl @Base id 1 {
      is_abstract = false, is_final = false, is_interface = false
    }
    simulation.class.decl @Derived id 2 extends @Base {
      is_abstract = false, is_final = true, is_interface = false
    }
    simulation.class.method @Base_run of @Base slot 0 signature_id 17
        implemented_by @base_run :
      (!simulation.context, !simulation.class_handle<@Base>, i32,
       !simulation.logic<8>) -> () {
        is_final = false, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.class.method @Derived_run of @Derived slot 0 signature_id 17
        implemented_by @derived_run :
      (!simulation.context, !simulation.class_handle<@Derived>, i32,
       !simulation.logic<8>) -> () {
        is_final = true, is_pure = false, is_static = false,
        is_task = true, is_virtual = true
      }
    simulation.func private @base_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32},
        %logic: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 1 : i64, entry_kind = 12 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      %local = simulation.ref.alloc %sum : i32 -> !simulation.ref<i32>
      %inverted = simulation.logic.unary bit_not %logic :
        (!simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
    simulation.func private @derived_run(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %this: !simulation.class_handle<@Derived> {simulation.capture_kind = 1 : i32},
        %value: i32 {simulation.capture_kind = 1 : i32},
        %logic: !simulation.logic<8> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 2 : i64, entry_kind = 12 : i32} {
      %one = arith.constant 1 : i32
      %sum = arith.addi %value, %one : i32
      %local = simulation.ref.alloc %sum : i32 -> !simulation.ref<i32>
      %inverted = simulation.logic.unary bit_not %logic :
        (!simulation.logic<8>) -> !simulation.logic<8>
      simulation.return
    }
    simulation.func @caller(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %receiver: !simulation.class_handle<@Base> {simulation.capture_kind = 1 : i32})
        attributes {code_unit_id = 3 : i64, entry_kind = 1 : i32} {
      %input = arith.constant 41 : i32
      %bits = arith.constant 9 : i8
      %known = simulation.logic.from_bits %bits : i8 -> !simulation.logic<8>
      simulation.class.virtual_task_call
        %receiver[@Base_run] slot 0 signature_id 17
        (%input, %known) arguments 2 to ^done :
        (!simulation.class_handle<@Base>, i32, !simulation.logic<8>) -> ()
    ^done:
      simulation.return
    }
  }
}

