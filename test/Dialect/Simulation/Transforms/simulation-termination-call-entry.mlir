// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=off}))' | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-simplify-bodies{vpi=full}))' | FileCheck %s --check-prefix=OPEN
// A private call chain inherits a clear finish latch only if every caller
// proves it. Public entry, a second unknown caller, and calls after $finish
// retain the conservative entry behavior. VPI keeps the call boundary open.
module {
  simulation.design @call_entry {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.code_unit.decl 1 in 0 always_comb hierarchy "scheduled"
    simulation.code_unit.decl 2 in 0 function hierarchy "middle"
    simulation.code_unit.decl 3 in 0 function hierarchy "leaf"
    simulation.code_unit.decl 4 in 0 function hierarchy "mixed"
    simulation.code_unit.decl 5 in 0 function hierarchy "unknown_entry"
    simulation.code_unit.decl 6 in 0 function hierarchy "after_finish"
    simulation.func @scheduled(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 4 : i32, code_unit_id = 1 : i64} {
      %in = simulation.context.storage %ctx[0] : !simulation.ref<i1>
      cf.br ^body
    ^body:
      %a = simulation.call @middle(%ctx) : (!simulation.context) -> i1
      %b = simulation.call @mixed(%ctx) : (!simulation.context) -> i1
      %zero = arith.constant 0 : i32
      simulation.finish %ctx, %zero
      %c = simulation.call @after_finish(%ctx) : (!simulation.context) -> i1
      simulation.suspend.change %in to ^body : !simulation.ref<i1>
    }
    // CHECK-LABEL: simulation.func private @middle
    // CHECK-NOT: simulation.termination.requested
    // CHECK: simulation.return
    // OPEN-LABEL: simulation.func private @middle
    // OPEN: simulation.termination.requested
    simulation.func private @middle(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %a = simulation.call @leaf(%ctx) : (!simulation.context) -> i1
      %poll = simulation.termination.requested %ctx
      simulation.return %poll : i1
    }
    // CHECK-LABEL: simulation.func private @leaf
    // CHECK-NOT: simulation.termination.requested
    // CHECK: simulation.return
    // OPEN-LABEL: simulation.func private @leaf
    // OPEN: simulation.termination.requested
    simulation.func private @leaf(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %poll = simulation.termination.requested %ctx
      simulation.return %poll : i1
    }
    // CHECK-LABEL: simulation.func private @mixed
    // CHECK: simulation.termination.requested
    simulation.func private @mixed(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %poll = simulation.termination.requested %ctx
      simulation.return %poll : i1
    }
    simulation.func @unknown_entry(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %a = simulation.call @mixed(%ctx) : (!simulation.context) -> i1
      simulation.return
    }
    // CHECK-LABEL: simulation.func private @after_finish
    // CHECK: simulation.termination.requested
    simulation.func private @after_finish(%ctx: !simulation.context {simulation.capture_kind = 0 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %poll = simulation.termination.requested %ctx
      simulation.return %poll : i1
    }
  }
}
