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
  obelisk_sim.design @controlled_pass_delay_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.left"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.right"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    obelisk_sim.code_unit.decl 9970000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9970001 in 0 initial hierarchy "top.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9970000 : i64,
                    obelisk_sim.lowered} {
      %driver = obelisk_sim.context.driver %ctx[0] :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %right = obelisk_sim.context.net %ctx[1] :
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @initial(%ctx, %driver, %right) :
          !obelisk_sim.context,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %driver: !obelisk_sim.driver<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 5 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %right: !obelisk_sim.net<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 4 : i32,
             obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9970001 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      %on = obelisk_sim.time.constant 3
      %off = obelisk_sim.time.constant 5
      %unknown = obelisk_sim.time.constant 3
      %stdout = arith.constant 1 : i32
      obelisk_sim.driver.drive %driver = %one :
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control_delayed 0 = %one
          after[%on, %off, %unknown] : !obelisk_sim.logic<1>
      %v0 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f0 = obelisk_sim.bytes.constant "initial %v"
      obelisk_sim.display %ctx to %stdout(%f0, %v0, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^before_on

    ^before_on:
      %stdout_before_on = arith.constant 1 : i32
      %v1 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f1 = obelisk_sim.bytes.constant "before-on %v"
      obelisk_sim.display %ctx to %stdout_before_on(%f1, %v1, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %two_more = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two_more to ^enabled

    ^enabled:
      %zero_enabled = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %on_enabled = obelisk_sim.time.constant 3
      %off_enabled = obelisk_sim.time.constant 5
      %unknown_enabled = obelisk_sim.time.constant 3
      %stdout_enabled = arith.constant 1 : i32
      %v2 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f2 = obelisk_sim.bytes.constant "enabled %v"
      obelisk_sim.display %ctx to %stdout_enabled(%f2, %v2, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.net.pass.control_delayed 0 = %zero_enabled
          after[%on_enabled, %off_enabled, %unknown_enabled] :
          !obelisk_sim.logic<1>
      %four = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %four to ^before_off

    ^before_off:
      %stdout_before_off = arith.constant 1 : i32
      %v3 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f3 = obelisk_sim.bytes.constant "before-off %v"
      obelisk_sim.display %ctx to %stdout_before_off(%f3, %v3, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      %two_off = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two_off to ^disabled

    ^disabled:
      %one_disabled = obelisk_sim.logic.constant true, false :
          !obelisk_sim.logic<1>
      %on_disabled = obelisk_sim.time.constant 3
      %off_disabled = obelisk_sim.time.constant 5
      %unknown_disabled = obelisk_sim.time.constant 3
      %stdout_disabled = arith.constant 1 : i32
      %v4 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f4 = obelisk_sim.bytes.constant "disabled %v"
      obelisk_sim.display %ctx to %stdout_disabled(%f4, %v4, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.net.pass.control_delayed 0 = %one_disabled
          after[%on_disabled, %off_disabled, %unknown_disabled] :
          !obelisk_sim.logic<1>
      %pulse = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %pulse to ^reject

    ^reject:
      %zero_reject = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      %on_reject = obelisk_sim.time.constant 3
      %off_reject = obelisk_sim.time.constant 5
      %unknown_reject = obelisk_sim.time.constant 3
      obelisk_sim.net.pass.control_delayed 0 = %zero_reject
          after[%on_reject, %off_reject, %unknown_reject] :
          !obelisk_sim.logic<1>
      %wait = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %wait to ^rejected

    ^rejected:
      %x_rejected = obelisk_sim.logic.constant false, true :
          !obelisk_sim.logic<1>
      %on_rejected = obelisk_sim.time.constant 3
      %off_rejected = obelisk_sim.time.constant 5
      %unknown_rejected = obelisk_sim.time.constant 3
      %stdout_rejected = arith.constant 1 : i32
      %v5 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f5 = obelisk_sim.bytes.constant "rejected %v"
      obelisk_sim.display %ctx to %stdout_rejected(%f5, %v5, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.net.pass.control_delayed 0 = %x_rejected
          after[%on_rejected, %off_rejected, %unknown_rejected] :
          !obelisk_sim.logic<1>
      %xwait = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %xwait to ^unknown_state

    ^unknown_state:
      %stdout_unknown = arith.constant 1 : i32
      %v6 = obelisk_sim.net.read %right :
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f6 = obelisk_sim.bytes.constant "unknown %v"
      obelisk_sim.display %ctx to %stdout_unknown(%f6, %v6, %right)
          newline = true radix = 10 flags = [0, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
