// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

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
      // CHECK: simulation.suspend.delay %{{.*}} to
      // CHECK-SAME: timing = #schedule.timing_site<id = 0, kind = deadline_slot>
      simulation.suspend.delay %delay to ^done
    ^done:
      simulation.return
    }
  }
}
