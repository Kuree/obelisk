// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph))' | FileCheck %s

// IEEE 1800-2017 9.2.2 requires an always procedure to repeat continuously,
// and 4.9.3 requires a blocking update to enable dependent events. Preserve
// the self-sensitivity edge for the outer wait so an update in one iteration
// can enqueue the next iteration.
// CHECK-LABEL: obelisk_sim.design @repeating_always attributes {compute_graph = #schedule.graph<
// CHECK-SAME: #schedule.fragment<id = [[WAIT:[0-9]+]], function = @process, block = 1
// CHECK-SAME: effect = watch
// CHECK-SAME: #schedule.fragment<id = [[BODY:[0-9]+]], function = @process, block = 2
// CHECK-SAME: effect = write
// CHECK-SAME: #schedule.edge<source = [[BODY]], target = [[WAIT]], kind = sensitivity
// CHECK-SAME: regions =
module {
  obelisk_sim.design @repeating_always {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 always hierarchy "top.process"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design

    obelisk_sim.func @process(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %state: !obelisk_sim.ref<!obelisk_sim.logic<1>> {
          obelisk_sim.capture_kind = 3 : i32,
          obelisk_sim.descriptor_id = 0 : i64
        }) attributes {entry_kind = 3 : i32, code_unit_id = 1 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %state to ^body {
          schedule.procedural_event_wait,
          schedule.repeating_always_wait
        } : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^body:
      %value = obelisk_sim.ref.load %state :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %value to %state : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
  }
}
