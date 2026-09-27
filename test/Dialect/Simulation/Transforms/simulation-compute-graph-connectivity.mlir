// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

// A publication expanded through a connected component still activates a
// compact subscription on the consumer's original descriptor.
// CHECK: edges = [#schedule.edge<source = 0, target = 2, kind = sensitivity, resource = <effect = watch, resource = net, target = descriptor, descriptor = 2

module {
  simulation.design @connectivity_graph {
    simulation.code_unit.decl 9000001 in 0 continuous hierarchy "test.connectivity_graph.driver.9000001"
    simulation.code_unit.decl 9000002 in 0 always hierarchy "test.connectivity_graph.reader.9000002"
    simulation.code_unit.decl 9000003 in 0 continuous hierarchy "test.connectivity_graph.packed_driver.9000003"
    simulation.scope.decl 0
    simulation.net.decl 0 in 0 : !simulation.logic<4> design
    simulation.net.decl 1 in 0 : !simulation.logic<4> design
    simulation.net.decl 2 in 0 : !simulation.logic<4> design
    simulation.net.decl 3 in 0 : !simulation.logic<4> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<4> design {driven_low = 1 : i64, driven_width = 1 : i64}
    simulation.driver.decl 1 in 0 drives 3 : !simulation.logic<4> design {driven_low = 0 : i64, driven_width = 4 : i64}
    simulation.net.connect.decl 0 in 0 0[0] to 1[3] width 2 reversed = true
    simulation.net.connect.decl 1 in 0 1[2] to 2[1] width 1 reversed = false

    // One physical drive is expanded through the reversed and transitive
    // aliases while retaining every logical descriptor for diagnostics.
    // CHECK-LABEL: simulation.func @driver
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 1, width = 1
    // CHECK-SAME: effect = drive, resource = net, target = descriptor, descriptor = 1, formal = 0, low = 2, width = 1
    // CHECK-SAME: effect = drive, resource = net, target = descriptor, descriptor = 2, formal = 0, low = 1, width = 1
    simulation.func @driver(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<4>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9000001 : i64} {
      %bit = simulation.driver.extract %driver from 1 : !simulation.driver<!simulation.logic<4>> -> !simulation.driver<!simulation.logic<1>>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %bit = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }

    // Consumers retain only the descriptor they name. Producers publish every
    // alias above, so this compact subscription is equivalent and avoids an
    // N-by-N summary for a net fanned out through N interface ports. Bits
    // outside the connected component remain dependencies of descriptor 2.
    // CHECK-LABEL: simulation.func @reader
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = read, resource = net, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 4
    // CHECK-SAME: effect = watch, resource = net, target = descriptor, descriptor = 2, formal = 0, low = 0, width = 4
    simulation.func @reader(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %net: !simulation.net<!simulation.logic<4>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000002 : i64} {
      %value = simulation.net.read %net : !simulation.net<!simulation.logic<4>> -> !simulation.logic<4>
      simulation.suspend.change %net to ^resume : !simulation.net<!simulation.logic<4>>
    ^resume:
      simulation.return
    }

    // Adjacent compatible packed effects are represented once. This keeps a
    // bit-blasted lowering from producing four graph edges and publications.
    // CHECK-LABEL: simulation.func @packed_driver
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 4
    simulation.func @packed_driver(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<4>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9000003 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %bit0 = simulation.driver.extract %driver from 0 : !simulation.driver<!simulation.logic<4>> -> !simulation.driver<!simulation.logic<1>>
      %bit1 = simulation.driver.extract %driver from 1 : !simulation.driver<!simulation.logic<4>> -> !simulation.driver<!simulation.logic<1>>
      %bit2 = simulation.driver.extract %driver from 2 : !simulation.driver<!simulation.logic<4>> -> !simulation.driver<!simulation.logic<1>>
      %bit3 = simulation.driver.extract %driver from 3 : !simulation.driver<!simulation.logic<4>> -> !simulation.driver<!simulation.logic<1>>
      simulation.driver.drive %bit0 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %bit1 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %bit2 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %bit3 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }
  }
}
