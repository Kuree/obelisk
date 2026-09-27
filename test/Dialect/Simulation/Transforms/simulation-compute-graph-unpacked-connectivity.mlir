// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

// IEEE 1800-2017 3.9.6 and 23.3.3: an output port continuously connects its
// local source to the outside net. Fixed unpacked-array elements therefore
// participate in the same static connectivity components as packed nets.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @unpacked_connectivity {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 continuous hierarchy "drive"
    simulation.net.decl 0 in 0 : !simulation.unpacked_array<0 : 1 x !simulation.logic<1>> design
    simulation.net.decl 1 in 0 : !simulation.logic<1> design
    simulation.driver.decl 0 in 0 drives 1 : !simulation.logic<1> design
    simulation.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false

    // CHECK-LABEL: simulation.func @drive
    // CHECK-SAME: effect_summary = [#schedule.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1
    // CHECK-SAME: effect = drive, resource = net, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 1
    simulation.func @drive(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %driver = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.return
    }

    // One resolved component is published to both its local and outside net.
    // NATIVE-LABEL: llvm.func @drive
    // NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_static_transition
    // NATIVE: llvm.return
  }
}
