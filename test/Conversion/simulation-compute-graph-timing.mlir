// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  obelisk_sim.design @timing {
    obelisk_sim.scope.decl 0

    obelisk_sim.func @constant_choice(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %choose: i1 {obelisk_sim.capture_kind = 2 : i32})
        attributes {entry_kind = 1 : i32} {
      %five = obelisk_sim.time.constant 5
      %ten = obelisk_sim.time.constant 10
      cf.cond_br %choose, ^left, ^right
    ^left:
      cf.br ^wait(%five : !obelisk_sim.time)
    ^right:
      cf.br ^wait(%ten : !obelisk_sim.time)
    ^wait(%delay: !obelisk_sim.time):
      // A runtime choice between distinct constants is a variable deadline,
      // not a single compiled-calendar site.
      // CHECK: obelisk_sim.suspend.delay %{{.*}} to
      // CHECK-SAME: timing = #obelisk_sim.timing_site<id = 0, kind = deadline_slot>
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      obelisk_sim.return
    }
  }
}
