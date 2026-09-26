// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
// RUN: FileCheck %s --check-prefix=NATIVE < %t.native.mlir

// Runtime behavior is checked in ../Runtime/simulation-inertial-path-cancellation.test.

// Returning to each of 0, 1, X and Z cancels the pending default-inertial
// update, even though comparison with the published value yields empty
// transition masks. Cancellation must preserve another bit's pending update
// and allow a subsequent activation to schedule a new transition.
// NATIVE: llvm.call @obelisk_rt_v1_scheduler_inertial_path_driver
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @inertial_path_cancellation {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<5> design
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<5> design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 2 in 0 initial hierarchy "top.test"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %driver = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<5>>
      %net = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<5>>
      %test = obelisk_sim.spawn @test(%ctx, %driver, %net) : !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.net<!obelisk_sim.logic<5>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @test(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<5>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %net: !obelisk_sim.net<!obelisk_sim.logic<5>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %initial = obelisk_sim.logic.constant 0 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %none = arith.constant 0 : i5
      %now = obelisk_sim.time.constant 0
      obelisk_sim.driver.drive_inertial_path %driver = %initial active %none masks[%none, %none, %none] after[%now, %now, %now] site 2 : 0 group 0 of 1 : !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.logic<5>, i5
      %settle = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %settle to ^establish
    ^establish:
      // Establish actual X/Z transitions after the initial path source.
      %base = obelisk_sim.logic.constant 10 : i5, 12 : i5 : !obelisk_sim.logic<5>
      %noPath = arith.constant 0 : i5
      %immediate = obelisk_sim.time.constant 0
      obelisk_sim.driver.drive_inertial_path %driver = %base active %noPath masks[%noPath, %noPath, %noPath] after[%immediate, %immediate, %immediate] site 2 : 0 group 0 of 1 : !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.logic<5>, i5
      %one = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one to ^start
    ^start:
      %v0 = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<5>> -> !obelisk_sim.logic<5>
      %f0 = obelisk_sim.bytes.constant "base %b"
      %stdout0 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout0(%f0, %v0) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<5>
      %target = obelisk_sim.logic.constant 21 : i5, 0 : i5 : !obelisk_sim.logic<5>
      %all = arith.constant 31 : i5
      %five = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %target active %all masks[%all, %all, %all] after[%five, %five, %five] site 2 : 0 group 0 of 1 : !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.logic<5>, i5
      %one1 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one1 to ^cancel
    ^cancel:
      %returned = obelisk_sim.logic.constant 26 : i5, 12 : i5 : !obelisk_sim.logic<5>
      %active = arith.constant 31 : i5
      %zeroMask = arith.constant 0 : i5
      %delay = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive_inertial_path %driver = %returned active %active masks[%zeroMask, %zeroMask, %zeroMask] after[%delay, %delay, %delay] site 2 : 0 group 0 of 2 : !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.logic<5>, i5
      obelisk_sim.driver.drive_inertial_path %driver = %returned active %active masks[%zeroMask, %zeroMask, %zeroMask] after[%delay, %delay, %delay] site 2 : 0 group 1 of 2 : !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.logic<5>, i5
      %one2 = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one2 to ^cancelled
    ^cancelled:
      %v1 = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<5>> -> !obelisk_sim.logic<5>
      %f1 = obelisk_sim.bytes.constant "cancelled %b"
      %stdout1 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout1(%f1, %v1) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<5>
      %four = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %four to ^independent
    ^independent:
      %v2 = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<5>> -> !obelisk_sim.logic<5>
      %f2 = obelisk_sim.bytes.constant "independent %b"
      %stdout2 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout2(%f2, %v2) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<5>
      %next = obelisk_sim.logic.constant 27 : i5, 12 : i5 : !obelisk_sim.logic<5>
      %bit = arith.constant 1 : i5
      %two = obelisk_sim.time.constant 2
      obelisk_sim.driver.drive_inertial_path %driver = %next active %bit masks[%bit, %bit, %bit] after[%two, %two, %two] site 2 : 0 group 0 of 1 : !obelisk_sim.driver<!obelisk_sim.logic<5>>, !obelisk_sim.logic<5>, i5
      %three = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three to ^done
    ^done:
      %v3 = obelisk_sim.net.read %net : !obelisk_sim.net<!obelisk_sim.logic<5>> -> !obelisk_sim.logic<5>
      %f3 = obelisk_sim.bytes.constant "reactivated %b"
      %stdout3 = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout3(%f3, %v3) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<5>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
