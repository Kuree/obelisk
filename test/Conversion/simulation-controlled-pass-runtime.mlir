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
  simulation.design @controlled_pass_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.left"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.middle"
    simulation.net.decl 2 in 0 : !simulation.logic<1> design hierarchy "top.right"
    simulation.net.decl 3 in 0 : !simulation.logic<4> design hierarchy "top.array_left"
    simulation.net.decl 4 in 0 : !simulation.logic<4> design hierarchy "top.array_right"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    simulation.driver.decl 1 in 0 drives 2 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    simulation.driver.decl 2 in 0 drives 3 : !simulation.logic<4> design {
      driven_low = 0 : i64, driven_width = 4 : i64
    }
    simulation.net.pass.decl 0 in 0 0[0] to 1[0] width 1 reversed = false {
      controlled = true
    }
    simulation.net.pass.decl 1 in 0 1[0] to 2[0] width 1 reversed = false {
      controlled = true, resistive = true
    }
    // Two declaration runs share one precomputed control group.  This is the
    // compact representation used for elaborated primitive arrays.
    simulation.net.pass.decl 2 in 0 3[0] to 4[3] width 2 reversed = true {
      controlled = true, control_group = 2 : i64
    }
    simulation.net.pass.decl 3 in 0 3[2] to 4[1] width 2 reversed = true {
      controlled = true, control_group = 2 : i64
    }
    simulation.code_unit.decl 9980000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9980001 in 0 initial hierarchy "top.check"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9980000 : i64} {
      %d0 = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %d1 = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %da = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<4>>
      %n0 = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %n1 = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %n2 = simulation.context.net %ctx[2] : !simulation.net<!simulation.logic<1>>
      %na = simulation.context.net %ctx[4] : !simulation.net<!simulation.logic<4>>
      %process = simulation.spawn @check(%ctx, %d0, %d1, %da, %n0, %n1, %n2, %na) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<4>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<4>> -> !simulation.process
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %d0: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %d1: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64},
        %da: !simulation.driver<!simulation.logic<4>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64},
        %n0: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %n1: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64},
        %n2: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64},
        %na: !simulation.net<!simulation.logic<4>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9980001 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %pattern = simulation.logic.constant 10 : i4, 0 : i4 : !simulation.logic<4>
      simulation.driver.drive %d0 = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.net.pass.control 0 = %one : !simulation.logic<1>
      simulation.net.pass.control 1 = %one : !simulation.logic<1>
      %v0 = simulation.net.read %n0 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v1 = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %v2 = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %enabled = simulation.bytes.constant "enabled %v %v %v"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%enabled, %v0, %n0, %v1, %n1, %v2, %n2)
          newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] :
          !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.net.pass.control 0 = %zero : !simulation.logic<1>
      %d0v = simulation.net.read %n0 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %d1v = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %d2v = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %disabled = simulation.bytes.constant "disabled %v %v %v"
      simulation.display %ctx to %stdout(%disabled, %d0v, %n0, %d1v, %n1, %d2v, %n2)
          newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] :
          !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.net.pass.control 0 = %x : !simulation.logic<1>
      %x0 = simulation.net.read %n0 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %x1 = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %x2 = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %xformat = simulation.bytes.constant "x-control %v %v %v"
      simulation.display %ctx to %stdout(%xformat, %x0, %n0, %x1, %n1, %x2, %n2)
          newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] :
          !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.net.pass.control 0 = %z : !simulation.logic<1>
      %z0 = simulation.net.read %n0 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %z1 = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %z2 = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %zformat = simulation.bytes.constant "z-control %v %v %v"
      simulation.display %ctx to %stdout(%zformat, %z0, %n0, %z1, %n1, %z2, %n2)
          newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] :
          !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.net.pass.control 0 = %one : !simulation.logic<1>
      simulation.driver.drive %d0 = %z : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %d1 = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %r0 = simulation.net.read %n0 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %r1 = simulation.net.read %n1 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %r2 = simulation.net.read %n2 : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %reverse = simulation.bytes.constant "reverse %v %v %v"
      simulation.display %ctx to %stdout(%reverse, %r0, %n0, %r1, %n1, %r2, %n2)
          newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] :
          !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>,
          !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.driver.drive %da = %pattern : !simulation.driver<!simulation.logic<4>>, !simulation.logic<4>
      simulation.net.pass.control 2 = %one : !simulation.logic<1>
      %array_value = simulation.net.read %na : !simulation.net<!simulation.logic<4>> -> !simulation.logic<4>
      %array_format = simulation.bytes.constant "array %b"
      simulation.display %ctx to %stdout(%array_format, %array_value)
          newline = true radix = <decimal> flags = [0, 0] : !simulation.bytes, !simulation.logic<4>
      simulation.return
    }
  }
}
