// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.native.mlir
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
  simulation.design @inertial_path_cancellation {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<5> design
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<5> design
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 2 in 0 initial hierarchy "top.test"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %driver = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<5>>
      %net = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<5>>
      %test = simulation.spawn @test(%ctx, %driver, %net) : !simulation.context, !simulation.driver<!simulation.logic<5>>, !simulation.net<!simulation.logic<5>> -> !simulation.process
      simulation.return
    }
    simulation.func @test(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<5>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %net: !simulation.net<!simulation.logic<5>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 2 : i64} {
      %initial = simulation.logic.constant 0 : i5, 0 : i5 : !simulation.logic<5>
      %none = arith.constant 0 : i5
      %now = simulation.time.constant 0
      simulation.driver.drive_inertial_path %driver = %initial active %none masks[%none, %none, %none] after[%now, %now, %now] site 2 : 0 group 0 of 1 : !simulation.driver<!simulation.logic<5>>, !simulation.logic<5>, i5
      %settle = simulation.time.constant 1
      simulation.suspend.delay %settle to ^establish
    ^establish:
      // Establish actual X/Z transitions after the initial path source.
      %base = simulation.logic.constant 10 : i5, 12 : i5 : !simulation.logic<5>
      %noPath = arith.constant 0 : i5
      %immediate = simulation.time.constant 0
      simulation.driver.drive_inertial_path %driver = %base active %noPath masks[%noPath, %noPath, %noPath] after[%immediate, %immediate, %immediate] site 2 : 0 group 0 of 1 : !simulation.driver<!simulation.logic<5>>, !simulation.logic<5>, i5
      %one = simulation.time.constant 1
      simulation.suspend.delay %one to ^start
    ^start:
      %v0 = simulation.net.read %net : !simulation.net<!simulation.logic<5>> -> !simulation.logic<5>
      %f0 = simulation.bytes.constant "base %b"
      %stdout0 = arith.constant 1 : i32
      simulation.display %ctx to %stdout0(%f0, %v0) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<5>
      %target = simulation.logic.constant 21 : i5, 0 : i5 : !simulation.logic<5>
      %all = arith.constant 31 : i5
      %five = simulation.time.constant 5
      simulation.driver.drive_inertial_path %driver = %target active %all masks[%all, %all, %all] after[%five, %five, %five] site 2 : 0 group 0 of 1 : !simulation.driver<!simulation.logic<5>>, !simulation.logic<5>, i5
      %one1 = simulation.time.constant 1
      simulation.suspend.delay %one1 to ^cancel
    ^cancel:
      %returned = simulation.logic.constant 26 : i5, 12 : i5 : !simulation.logic<5>
      %active = arith.constant 31 : i5
      %zeroMask = arith.constant 0 : i5
      %delay = simulation.time.constant 5
      simulation.driver.drive_inertial_path %driver = %returned active %active masks[%zeroMask, %zeroMask, %zeroMask] after[%delay, %delay, %delay] site 2 : 0 group 0 of 2 : !simulation.driver<!simulation.logic<5>>, !simulation.logic<5>, i5
      simulation.driver.drive_inertial_path %driver = %returned active %active masks[%zeroMask, %zeroMask, %zeroMask] after[%delay, %delay, %delay] site 2 : 0 group 1 of 2 : !simulation.driver<!simulation.logic<5>>, !simulation.logic<5>, i5
      %one2 = simulation.time.constant 1
      simulation.suspend.delay %one2 to ^cancelled
    ^cancelled:
      %v1 = simulation.net.read %net : !simulation.net<!simulation.logic<5>> -> !simulation.logic<5>
      %f1 = simulation.bytes.constant "cancelled %b"
      %stdout1 = arith.constant 1 : i32
      simulation.display %ctx to %stdout1(%f1, %v1) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<5>
      %four = simulation.time.constant 4
      simulation.suspend.delay %four to ^independent
    ^independent:
      %v2 = simulation.net.read %net : !simulation.net<!simulation.logic<5>> -> !simulation.logic<5>
      %f2 = simulation.bytes.constant "independent %b"
      %stdout2 = arith.constant 1 : i32
      simulation.display %ctx to %stdout2(%f2, %v2) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<5>
      %next = simulation.logic.constant 27 : i5, 12 : i5 : !simulation.logic<5>
      %bit = arith.constant 1 : i5
      %two = simulation.time.constant 2
      simulation.driver.drive_inertial_path %driver = %next active %bit masks[%bit, %bit, %bit] after[%two, %two, %two] site 2 : 0 group 0 of 1 : !simulation.driver<!simulation.logic<5>>, !simulation.logic<5>, i5
      %three = simulation.time.constant 3
      simulation.suspend.delay %three to ^done
    ^done:
      %v3 = simulation.net.read %net : !simulation.net<!simulation.logic<5>> -> !simulation.logic<5>
      %f3 = simulation.bytes.constant "reactivated %b"
      %stdout3 = arith.constant 1 : i32
      simulation.display %ctx to %stdout3(%f3, %v3) newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<5>
      %status = arith.constant 0 : i32
      simulation.finish %ctx, %status
      simulation.return
    }
  }
}
