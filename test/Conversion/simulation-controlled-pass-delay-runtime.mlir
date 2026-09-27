// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// Runtime behavior is checked in ../Runtime/simulation-controlled-pass-delay-runtime.test.

// Hand-authored Simulation IR checks the IEEE 1800-2017 28.8 topology-state
// delays directly. Signals still cross an enabled channel immediately.
// Turn-on is 3 ticks, turn-off is 5, and X/Z uses min(3,5). A rejected
// one-tick enable pulse never changes connectivity.
// LLVM: llvm.call @obelisk_rt_v1_pass_switch_control_delayed
// BYTECODE: intrinsic {{[0-9]+}}: id=0x0001023e

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @controlled_pass_delay_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.left"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.right"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    simulation.code_unit.decl 9970000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9970001 in 0 initial hierarchy "top.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64,
                    simulation.lowered} {
      %driver = simulation.context.driver %ctx[0] :
          !simulation.driver<!simulation.logic<1>>
      %right = simulation.context.net %ctx[1] :
          !simulation.net<!simulation.logic<1>>
      %process = simulation.spawn @initial(%ctx, %driver, %right) :
          !simulation.context,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %driver: !simulation.driver<!simulation.logic<1>>
            {simulation.capture_kind = 5 : i32,
             simulation.descriptor_id = 0 : i64},
        %right: !simulation.net<!simulation.logic<1>>
            {simulation.capture_kind = 4 : i32,
             simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970001 : i64,
                    simulation.lowered} {
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      %on = simulation.time.constant 3
      %off = simulation.time.constant 5
      %unknown = simulation.time.constant 3
      %stdout = arith.constant 1 : i32
      simulation.driver.drive %driver = %one :
          !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.net.pass.control_delayed 0 = %one
          after[%on, %off, %unknown] : !simulation.logic<1>
      %v0 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f0 = simulation.bytes.constant "initial %v"
      simulation.display %ctx to %stdout(%f0, %v0, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      %two = simulation.time.constant 2
      simulation.suspend.delay %two to ^before_on

    ^before_on:
      %stdout_before_on = arith.constant 1 : i32
      %v1 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f1 = simulation.bytes.constant "before-on %v"
      simulation.display %ctx to %stdout_before_on(%f1, %v1, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      %two_more = simulation.time.constant 2
      simulation.suspend.delay %two_more to ^enabled

    ^enabled:
      %zero_enabled = simulation.logic.constant false, false :
          !simulation.logic<1>
      %on_enabled = simulation.time.constant 3
      %off_enabled = simulation.time.constant 5
      %unknown_enabled = simulation.time.constant 3
      %stdout_enabled = arith.constant 1 : i32
      %v2 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f2 = simulation.bytes.constant "enabled %v"
      simulation.display %ctx to %stdout_enabled(%f2, %v2, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      simulation.net.pass.control_delayed 0 = %zero_enabled
          after[%on_enabled, %off_enabled, %unknown_enabled] :
          !simulation.logic<1>
      %four = simulation.time.constant 4
      simulation.suspend.delay %four to ^before_off

    ^before_off:
      %stdout_before_off = arith.constant 1 : i32
      %v3 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f3 = simulation.bytes.constant "before-off %v"
      simulation.display %ctx to %stdout_before_off(%f3, %v3, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      %two_off = simulation.time.constant 2
      simulation.suspend.delay %two_off to ^disabled

    ^disabled:
      %one_disabled = simulation.logic.constant true, false :
          !simulation.logic<1>
      %on_disabled = simulation.time.constant 3
      %off_disabled = simulation.time.constant 5
      %unknown_disabled = simulation.time.constant 3
      %stdout_disabled = arith.constant 1 : i32
      %v4 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f4 = simulation.bytes.constant "disabled %v"
      simulation.display %ctx to %stdout_disabled(%f4, %v4, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      simulation.net.pass.control_delayed 0 = %one_disabled
          after[%on_disabled, %off_disabled, %unknown_disabled] :
          !simulation.logic<1>
      %pulse = simulation.time.constant 1
      simulation.suspend.delay %pulse to ^reject

    ^reject:
      %zero_reject = simulation.logic.constant false, false :
          !simulation.logic<1>
      %on_reject = simulation.time.constant 3
      %off_reject = simulation.time.constant 5
      %unknown_reject = simulation.time.constant 3
      simulation.net.pass.control_delayed 0 = %zero_reject
          after[%on_reject, %off_reject, %unknown_reject] :
          !simulation.logic<1>
      %wait = simulation.time.constant 4
      simulation.suspend.delay %wait to ^rejected

    ^rejected:
      %x_rejected = simulation.logic.constant false, true :
          !simulation.logic<1>
      %on_rejected = simulation.time.constant 3
      %off_rejected = simulation.time.constant 5
      %unknown_rejected = simulation.time.constant 3
      %stdout_rejected = arith.constant 1 : i32
      %v5 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f5 = simulation.bytes.constant "rejected %v"
      simulation.display %ctx to %stdout_rejected(%f5, %v5, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      simulation.net.pass.control_delayed 0 = %x_rejected
          after[%on_rejected, %off_rejected, %unknown_rejected] :
          !simulation.logic<1>
      %xwait = simulation.time.constant 4
      simulation.suspend.delay %xwait to ^unknown_state

    ^unknown_state:
      %stdout_unknown = arith.constant 1 : i32
      %v6 = simulation.net.read %right :
          !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f6 = simulation.bytes.constant "unknown %v"
      simulation.display %ctx to %stdout_unknown(%f6, %v6, %right)
          newline = true radix = <decimal> flags = [0, 2048] :
          !simulation.bytes, !simulation.logic<1>,
          !simulation.net<!simulation.logic<1>>
      simulation.return
    }
  }
}
