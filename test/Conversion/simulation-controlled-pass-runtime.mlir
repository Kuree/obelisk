// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// Runtime behavior is checked in ../Runtime/simulation-controlled-pass-runtime.test.

// Hand-authored simulation IR exercises the runtime representation directly:
// full four-state controls, exact nonresistive/resistive strength propagation,
// bidirectional flow, and one reversed packed pass-switch run.
// LLVM-COUNT-3: llvm.call @obelisk_rt_v1_pass_switch_control
// BYTECODE-COUNT-3: intrinsic {{[0-9]+}}: id=0x0001023d

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @controlled_pass_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.left"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.middle"
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design hierarchy "top.right"
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<4> design hierarchy "top.array_left"
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<4> design hierarchy "top.array_right"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    obelisk_sim.driver.decl 1 in 0 drives 2 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    obelisk_sim.driver.decl 2 in 0 drives 3 : !obelisk_sim.logic<4> design {
      driven_low = 0 : i64, driven_width = 4 : i64
    }
    obelisk_sim.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    obelisk_sim.net.pass.decl 1 in 0 1[0] to 2[0] width 1 reversed = false {
      controlled = true, resistive = true
    }
    // Two declaration runs share one precomputed control group.  This is the
    // compact representation used for elaborated primitive arrays.
    obelisk_sim.net.pass.decl 2 in 0 3[0] to 4[3] width 2 reversed = true {
      controlled = true, control_group = 2 : i64
    }
    obelisk_sim.net.pass.decl 3 in 0 3[2] to 4[1] width 2 reversed = true {
      controlled = true, control_group = 2 : i64
    }
    obelisk_sim.code_unit.decl 9980000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9980001 in 0 initial hierarchy "top.check"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9980000 : i64} {
      %d0 = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %d1 = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %da = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<4>>
      %n0 = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n1 = obelisk_sim.context.net %ctx[1] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %n2 = obelisk_sim.context.net %ctx[2] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %na = obelisk_sim.context.net %ctx[4] : !obelisk_sim.net<!obelisk_sim.logic<4>>
      %process = obelisk_sim.spawn @check(%ctx, %d0, %d1, %da, %n0, %n1, %n2, %na) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<4>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %d0: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %d1: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %da: !obelisk_sim.driver<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %n0: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %n1: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %n2: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %na: !obelisk_sim.net<!obelisk_sim.logic<4>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 4 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9980001 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %pattern = obelisk_sim.logic.constant 10 : i4, 0 : i4 : !obelisk_sim.logic<4>
      obelisk_sim.driver.drive %d0 = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control 0 = %one : !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control 1 = %one : !obelisk_sim.logic<1>
      %v0 = obelisk_sim.net.read %n0 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %v2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %enabled = obelisk_sim.bytes.constant "enabled %v %v %v"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%enabled, %v0, %n0, %v1, %n1, %v2, %n2)
          newline = true radix = 10 flags = [0, 2048, 2048, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.net.pass.control 0 = %zero : !obelisk_sim.logic<1>
      %d0v = obelisk_sim.net.read %n0 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %d1v = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %d2v = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %disabled = obelisk_sim.bytes.constant "disabled %v %v %v"
      obelisk_sim.display %ctx to %stdout(%disabled, %d0v, %n0, %d1v, %n1, %d2v, %n2)
          newline = true radix = 10 flags = [0, 2048, 2048, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.net.pass.control 0 = %x : !obelisk_sim.logic<1>
      %x0 = obelisk_sim.net.read %n0 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %x2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %xformat = obelisk_sim.bytes.constant "x-control %v %v %v"
      obelisk_sim.display %ctx to %stdout(%xformat, %x0, %n0, %x1, %n1, %x2, %n2)
          newline = true radix = 10 flags = [0, 2048, 2048, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.net.pass.control 0 = %z : !obelisk_sim.logic<1>
      %z0 = obelisk_sim.net.read %n0 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %z1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %z2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %zformat = obelisk_sim.bytes.constant "z-control %v %v %v"
      obelisk_sim.display %ctx to %stdout(%zformat, %z0, %n0, %z1, %n1, %z2, %n2)
          newline = true radix = 10 flags = [0, 2048, 2048, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.net.pass.control 0 = %one : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d0 = %z : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %d1 = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %r0 = obelisk_sim.net.read %n0 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %r1 = obelisk_sim.net.read %n1 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %r2 = obelisk_sim.net.read %n2 : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %reverse = obelisk_sim.bytes.constant "reverse %v %v %v"
      obelisk_sim.display %ctx to %stdout(%reverse, %r0, %n0, %r1, %n1, %r2, %n2)
          newline = true radix = 10 flags = [0, 2048, 2048, 2048] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.driver.drive %da = %pattern : !obelisk_sim.driver<!obelisk_sim.logic<4>>, !obelisk_sim.logic<4>
      obelisk_sim.net.pass.control 2 = %one : !obelisk_sim.logic<1>
      %array_value = obelisk_sim.net.read %na : !obelisk_sim.net<!obelisk_sim.logic<4>> -> !obelisk_sim.logic<4>
      %array_format = obelisk_sim.bytes.constant "array %b"
      obelisk_sim.display %ctx to %stdout(%array_format, %array_value)
          newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<4>
      obelisk_sim.return
    }
  }
}
