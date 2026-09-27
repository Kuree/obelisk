// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // A trigger port connection has a sensitivity edge to the waiter, while the
  // independent data connection does not. Once both are ready, internal port
  // propagation must settle before the procedural waiter observes the trigger.
  // CHECK: compute_graph = #schedule.graph<
  // CHECK-SAME: regions = [#schedule.region<kind = active, groups = [
  // CHECK-SAME: #schedule.group<fragments = [0]
  // CHECK-SAME: #schedule.group<fragments = [1]
  // CHECK-SAME: #schedule.group<fragments = [4]
  // CHECK-SAME: #schedule.group<fragments = [5]
  // CHECK-SAME: #schedule.group<fragments = [2]
  // CHECK-SAME: #schedule.group<fragments = [3]
  simulation.design @settle_priority {
    simulation.code_unit.decl 1 in 0 initial hierarchy "waiter"
    simulation.code_unit.decl 2 in 0 port_output hierarchy "trigger_port" {internal}
    simulation.code_unit.decl 3 in 0 port_output hierarchy "data_port" {internal}
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : i1 design
    simulation.storage.decl 1 in 0 : i1 design
    simulation.storage.decl 2 in 0 : i1 design
    simulation.storage.decl 3 in 0 : i1 design

    simulation.func @m_waiter(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %data: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %trigger: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %data : !simulation.ref<i1> -> i1
      simulation.suspend.edge posedge %trigger to ^loop : !simulation.ref<i1>
    }

    simulation.func @a_trigger_port(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %output: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64},
        %internal: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64, internal} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %internal : !simulation.ref<i1> -> i1
      simulation.ref.store %value to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %internal to ^loop : !simulation.ref<i1>
    }

    simulation.func @z_data_port(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %output: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64},
        %internal: !simulation.ref<i1> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 3 : i64, internal} {
      cf.br ^loop
    ^loop:
      %value = simulation.ref.load %internal : !simulation.ref<i1> -> i1
      simulation.ref.store %value to %output : i1, !simulation.ref<i1>
      simulation.suspend.change %internal to ^loop : !simulation.ref<i1>
    }
  }
}
