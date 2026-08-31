// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' | FileCheck %s
// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=NATIVE

// IEEE 1800-2017 3.9.6 and 23.3.3: an output port continuously connects its
// local source to the outside net. Fixed unpacked-array elements therefore
// participate in the same static connectivity components as packed nets.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @unpacked_connectivity {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 continuous hierarchy "drive"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.unpacked_array<0 : 1 x !obelisk_sim.logic<1>> design
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.driver.decl 0 in 0 drives 1 : !obelisk_sim.logic<1> design
    obelisk_sim.net.connect.decl 0 in 0 0[0] to 1[0] width 1 reversed = false

    // CHECK-LABEL: obelisk_sim.func @drive
    // CHECK-SAME: effect_summary = [#obelisk_sim.effect<effect = drive, resource = net, target = descriptor, descriptor = 0, formal = 0, low = 0, width = 1
    // CHECK-SAME: effect = drive, resource = net, target = descriptor, descriptor = 1, formal = 0, low = 0, width = 1
    obelisk_sim.func @drive(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 7 : i32, code_unit_id = 1 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %driver = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    // One resolved component is published to both its local and outside net.
    // NATIVE-LABEL: llvm.func @drive
    // NATIVE-COUNT-2: llvm.call @obelisk_rt_v1_scheduler_static_transition
    // NATIVE: llvm.return
  }
}
