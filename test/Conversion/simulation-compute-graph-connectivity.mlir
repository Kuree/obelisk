// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s

module {
  obelisk_sim.design @connectivity_graph {
    obelisk_sim.code_unit.decl 9000001 in 0 continuous hierarchy "test.connectivity_graph.driver.9000001"
    obelisk_sim.code_unit.decl 9000002 in 0 always hierarchy "test.connectivity_graph.reader.9000002"
    obelisk_sim.code_unit.decl 9000003 in 0 continuous hierarchy "test.connectivity_graph.packed_driver.9000003"
    obelisk_sim.scope.decl 0
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<4> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<4> design {driven_low = 1 : i64, driven_width = 1 : i64}
    obelisk_sim.driver.decl 1 in 0 drives 3 : !obelisk_sim.logic<4> design {driven_low = 0 : i64, driven_width = 4 : i64}
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[3] width 2 reversed = true
    obelisk_sim.net.connect.decl 1 in 0 1[2] to 2[1] width 1 reversed = false

    // One physical drive is expanded through the reversed and transitive
    // aliases while retaining every logical descriptor for diagnostics.
    // CHECK-LABEL: obelisk_sim.func @driver
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 1, width = 1
    // CHECK-SAME: effect = drive, resource = net, target = descriptor, descriptor = 1, formal = 0, low = 2, width = 1
    // CHECK-SAME: effect = drive, resource = net, target = descriptor, descriptor = 2, formal = 0, low = 1, width = 1
    obelisk_sim.func @driver(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9000001 : i64} {
      %bit = obelisk_sim.driver.extract %driver from 1 : !obelisk_sim.driver<!obelisk_sim.logic<4>> -> !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    // Reads and transition watches are likewise expanded through topology.
    // CHECK-LABEL: obelisk_sim.func @reader
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = read, resource = net, target = descriptor, descriptor = 0
    // CHECK-SAME: effect = read, resource = net, target = descriptor, descriptor = 1
    // CHECK-SAME: effect = read, resource = net, target = descriptor, descriptor = 2
    // CHECK-SAME: effect = watch, resource = net, target = descriptor, descriptor = 0
    // CHECK-SAME: effect = watch, resource = net, target = descriptor, descriptor = 1
    // CHECK-SAME: effect = watch, resource = net, target = descriptor, descriptor = 2
    obelisk_sim.func @reader(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %net: !obelisk_sim.net<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 9000002 : i64} {
      %value = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      obelisk_sim.suspend.change %net to ^resume : !obelisk_sim.net<!obelisk_sim.logic<4>>
    ^resume:
      obelisk_sim.return
    }

    // Adjacent compatible packed effects are represented once. This keeps a
    // bit-blasted lowering from producing four graph edges and publications.
    // CHECK-LABEL: obelisk_sim.func @packed_driver
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 3, formal = 0, low = 0, width = 4
    obelisk_sim.func @packed_driver(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 9000003 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %bit0 = obelisk_sim.driver.extract %driver from 0 : !obelisk_sim.driver<!obelisk_sim.logic<4>> -> !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %bit1 = obelisk_sim.driver.extract %driver from 1 : !obelisk_sim.driver<!obelisk_sim.logic<4>> -> !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %bit2 = obelisk_sim.driver.extract %driver from 2 : !obelisk_sim.driver<!obelisk_sim.logic<4>> -> !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %bit3 = obelisk_sim.driver.extract %driver from 3 : !obelisk_sim.driver<!obelisk_sim.logic<4>> -> !obelisk_sim.driver<!obelisk_sim.logic<1>>
      obelisk_sim.driver.drive %bit0 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit1 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit2 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %bit3 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
