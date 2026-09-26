// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  // A trigger port connection has a sensitivity edge to the waiter, while the
  // independent data connection does not. Once both are ready, internal port
  // propagation must settle before the procedural waiter observes the trigger.
  // CHECK: compute_graph = #obelisk_sim.graph<
  // CHECK-SAME: regions = [#obelisk_sim.region<kind = active, groups = [
  // CHECK-SAME: #obelisk_sim.group<fragments = [0]
  // CHECK-SAME: #obelisk_sim.group<fragments = [1]
  // CHECK-SAME: #obelisk_sim.group<fragments = [4]
  // CHECK-SAME: #obelisk_sim.group<fragments = [5]
  // CHECK-SAME: #obelisk_sim.group<fragments = [2]
  // CHECK-SAME: #obelisk_sim.group<fragments = [3]
  obelisk_sim.design @settle_priority {
    obelisk_sim.code_unit.decl 1 in 0 initial hierarchy "waiter"
    obelisk_sim.code_unit.decl 2 in 0 port_output hierarchy "trigger_port" {internal}
    obelisk_sim.code_unit.decl 3 in 0 port_output hierarchy "data_port" {internal}
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : i1 design
    obelisk_sim.storage.decl 2 in 0 : i1 design
    obelisk_sim.storage.decl 3 in 0 : i1 design

    obelisk_sim.func @m_waiter(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %data: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %trigger: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 1 : i64} {
      cf.br ^loop
    ^loop:
      %value = obelisk_sim.ref.load %data : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.suspend.edge posedge %trigger to ^loop : !obelisk_sim.ref<i1>
    }

    obelisk_sim.func @a_trigger_port(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %output: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %internal: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 2 : i64, internal} {
      cf.br ^loop
    ^loop:
      %value = obelisk_sim.ref.load %internal : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.ref.store %value to %output : i1, !obelisk_sim.ref<i1>
      obelisk_sim.suspend.change %internal to ^loop : !obelisk_sim.ref<i1>
    }

    obelisk_sim.func @z_data_port(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %output: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %internal: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 10 : i32, code_unit_id = 3 : i64, internal} {
      cf.br ^loop
    ^loop:
      %value = obelisk_sim.ref.load %internal : !obelisk_sim.ref<i1> -> i1
      obelisk_sim.ref.store %value to %output : i1, !obelisk_sim.ref<i1>
      obelisk_sim.suspend.change %internal to ^loop : !obelisk_sim.ref<i1>
    }
  }
}
