// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// CHECK-DAG: simulation.suspend.delay {{.*}}timing = #schedule.timing_site<id = 0, kind = deadline_slot>
// CHECK-DAG: simulation.suspend.delay {{.*}}timing = #schedule.timing_site<id = 1, kind = calendar>
// CHECK-DAG: simulation.suspend.delay {{.*}}timing = #schedule.timing_site<id = 2, kind = calendar>

module {
  simulation.design @timing {
    simulation.code_unit.decl 9000001 in 0 initial hierarchy "test.timing.constant_choice.9000001"
    simulation.scope.decl 0

    simulation.func @constant_choice(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9000001 : i64} {
      %five = simulation.time.constant 5
      %ten = simulation.time.constant 10
      cf.cond_br %choose, ^left, ^right
    ^left:
      cf.br ^wait(%five : !simulation.time)
    ^right:
      cf.br ^wait(%ten : !simulation.time)
    ^wait(%delay: !simulation.time):
      // A runtime choice between distinct constants is a variable deadline,
      // not a single compiled-calendar site.
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
    simulation.code_unit.decl 2 in 0 initial hierarchy "same_constants"
    simulation.code_unit.decl 3 in 0 initial hierarchy "forward_loop"
    // Equal constants from distinct definitions survive a join.
    simulation.func @same_constants(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %choose: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %a = simulation.time.constant 5
      %b = simulation.time.constant 5
      cf.cond_br %choose, ^left, ^right
    ^left:
      cf.br ^wait(%a : !simulation.time)
    ^right:
      cf.br ^wait(%b : !simulation.time)
    ^wait(%delay: !simulation.time):
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
    // A cyclic forwarding lane reaches the initial constant without a
    // recursive backward traversal per timing site.
    simulation.func @forward_loop(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %repeat: i1 {simulation.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %five = simulation.time.constant 5
      cf.br ^loop(%five : !simulation.time)
    ^loop(%current: !simulation.time):
      cf.cond_br %repeat, ^loop(%current : !simulation.time), ^wait(%current : !simulation.time)
    ^wait(%delay: !simulation.time):
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
  }
}
