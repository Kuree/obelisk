// RUN: obelisk-opt %s --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=LLVM
// RUN: obelisk-opt %s --encode-obelisk-sim-to-bytecode='vpi=off require-bytecode=true' | %python %S/Inputs/dump-bytecode-instructions.py --state | FileCheck %s --check-prefix=BYTECODE

// Runtime behavior is checked in ../Runtime/simulation-delayed-mos-strength-runtime.test.

// This is deliberately hand-authored Simulation IR: source parsing is tested
// separately. It proves exact strength payloads, all three delay banks,
// initial possible conduction, source/control replacement while pending,
// same-logic strength changes, force/release, resistive transfer, downstream
// pass propagation, and the absence of destination-to-source backflow.
// LLVM: llvm.call @obelisk_rt_v1_mos_drive_delayed
// BYTECODE: intrinsic {{[0-9]+}}: id=0x0001023f

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @delayed_mos_strength_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.source"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.out"
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design hierarchy "top.rout"
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design hierarchy "top.downstream"
    obelisk_sim.net.decl 4 in 0 : !obelisk_sim.logic<1> design hierarchy "top.blocked"
    obelisk_sim.net.decl 5 in 0 : !obelisk_sim.logic<1> design hierarchy "top.source_upstream"
    obelisk_sim.net.decl 6 in 0 : !obelisk_sim.logic<1> design hierarchy "top.chain"
    obelisk_sim.net.decl 7 in 0 : !obelisk_sim.logic<1> design hierarchy "top.cycle_a"
    obelisk_sim.net.decl 8 in 0 : !obelisk_sim.logic<1> design hierarchy "top.cycle_b"
    obelisk_sim.driver.decl 0 in 0 drives 5 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    obelisk_sim.driver.decl 1 in 0 drives 5 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 5 : i32, strength1 = 5 : i32
    }
    obelisk_sim.driver.decl 2 in 0 drives 4 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    obelisk_sim.driver.decl 3 in 0 drives 7 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 5 : i32, strength1 = 5 : i32
    }
    obelisk_sim.net.pass.decl 0 in 0 1[0] to 0[0] width 1 reversed = false {
      control_group = 0 : i64, controlled = true, delayed = true,
      directed = true
    }
    obelisk_sim.net.pass.decl 1 in 0 2[0] to 0[0] width 1 reversed = false {
      control_group = 1 : i64, controlled = true, delayed = true,
      directed = true, resistive = true
    }
    obelisk_sim.net.pass.decl 2 in 0 4[0] to 0[0] width 1 reversed = false {
      control_group = 2 : i64, controlled = true, delayed = true,
      directed = true
    }
    obelisk_sim.net.pass.decl 3 in 0 1[0] to 3[0] width 1 reversed = false {
      resistive = true
    }
    obelisk_sim.net.pass.decl 4 in 0 0[0] to 5[0] width 1 reversed = false
    obelisk_sim.net.pass.decl 5 in 0 6[0] to 1[0] width 1 reversed = false {
      control_group = 5 : i64, controlled = true, delayed = true,
      directed = true
    }
    obelisk_sim.net.pass.decl 6 in 0 8[0] to 7[0] width 1 reversed = false {
      control_group = 6 : i64, controlled = true, delayed = true,
      directed = true
    }
    obelisk_sim.net.pass.decl 7 in 0 7[0] to 8[0] width 1 reversed = false {
      control_group = 7 : i64, controlled = true, delayed = true,
      directed = true
    }
    obelisk_sim.code_unit.decl 9991000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9991001 in 0 initial hierarchy "top.initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9991000 : i64,
                    obelisk_sim.lowered} {
      %supply = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %pull = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %block = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %cycle_driver = obelisk_sim.context.driver %ctx[3] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %out = obelisk_sim.context.net %ctx[1] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %rout = obelisk_sim.context.net %ctx[2] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %downstream = obelisk_sim.context.net %ctx[3] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %blocked = obelisk_sim.context.net %ctx[4] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %chain = obelisk_sim.context.net %ctx[6] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %cycle_a = obelisk_sim.context.net %ctx[7] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %cycle_b = obelisk_sim.context.net %ctx[8] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @initial(%ctx, %supply, %pull, %block,
          %cycle_driver, %source, %out, %rout, %downstream, %blocked, %chain,
          %cycle_a, %cycle_b) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %supply: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %pull: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %block: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %cycle_driver: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %out: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %rout: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %downstream: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 3 : i64},
        %blocked: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 4 : i64},
        %chain: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 6 : i64},
        %cycle_a: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 7 : i64},
        %cycle_b: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 8 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9991001 : i64,
                    obelisk_sim.lowered} {
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %rise = obelisk_sim.time.constant 2
      %fall = obelisk_sim.time.constant 3
      %turnoff = obelisk_sim.time.constant 5
      obelisk_sim.driver.drive %supply = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %pull = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %block = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %cycle_driver = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 0 = %x after[%rise, %fall, %turnoff] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 1 = %x after[%rise, %fall, %turnoff] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 2 = %x after[%rise, %fall, %turnoff] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 5 = %x after[%rise, %fall, %turnoff] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 6 = %one after[%rise, %fall, %turnoff] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 7 = %one after[%rise, %fall, %turnoff] : !obelisk_sim.logic<1>
      %three = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three to ^initial_x

    ^initial_x:
      %stdout0 = arith.constant 1 : i32
      %ov0 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %rv0 = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %bv0 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt0 = obelisk_sim.bytes.constant "initial-x %v %v %v"
      obelisk_sim.display %ctx to %stdout0(%fmt0, %ov0, %out, %rv0, %rout, %bv0, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      %one0 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %r0 = obelisk_sim.time.constant 2
      %f0 = obelisk_sim.time.constant 3
      %t0 = obelisk_sim.time.constant 5
      obelisk_sim.net.mos.drive_delayed 0 = %one0 after[%r0, %f0, %t0] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 1 = %one0 after[%r0, %f0, %t0] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 2 = %one0 after[%r0, %f0, %t0] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 5 = %one0 after[%r0, %f0, %t0] : !obelisk_sim.logic<1>
      %wait0 = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %wait0 to ^on

    ^on:
      %stdout1 = arith.constant 1 : i32
      %ov1 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %rv1 = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %dv1 = obelisk_sim.net.read %downstream : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %bv1 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt1 = obelisk_sim.bytes.constant "on %v %v %v %v"
      obelisk_sim.display %ctx to %stdout1(%fmt1, %ov1, %out, %rv1, %rout, %dv1, %downstream, %bv1, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      %z1 = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %supply = %z1 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %wait1 = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %wait1 to ^strength_only

    ^strength_only:
      %stdout2 = arith.constant 1 : i32
      %ov2 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %rv2 = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %dv2 = obelisk_sim.net.read %downstream : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %bv2 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt2 = obelisk_sim.bytes.constant "strength-only %v %v %v %v"
      obelisk_sim.display %ctx to %stdout2(%fmt2, %ov2, %out, %rv2, %rout, %dv2, %downstream, %bv2, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      %zero2 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %pull = %zero2 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %pulse = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %pulse to ^cancel

    ^cancel:
      %one3 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %pull = %one3 : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %wait3 = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %wait3 to ^rejected

    ^rejected:
      %stdout3 = arith.constant 1 : i32
      %ov3 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %dv3 = obelisk_sim.net.read %downstream : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %cv3 = obelisk_sim.net.read %chain : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %bv3 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt3 = obelisk_sim.bytes.constant "rejected %v %v %v %v"
      obelisk_sim.display %ctx to %stdout3(%fmt3, %ov3, %out, %dv3, %downstream, %cv3, %chain, %bv3, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      %off3 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %r3 = obelisk_sim.time.constant 2
      %f3 = obelisk_sim.time.constant 3
      %t3 = obelisk_sim.time.constant 5
      obelisk_sim.net.mos.drive_delayed 0 = %off3 after[%r3, %f3, %t3] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 1 = %off3 after[%r3, %f3, %t3] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 2 = %off3 after[%r3, %f3, %t3] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 5 = %off3 after[%r3, %f3, %t3] : !obelisk_sim.logic<1>
      %four3 = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %four3 to ^turnoff_pending

    ^turnoff_pending:
      %stdout4 = arith.constant 1 : i32
      %ov4 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt4 = obelisk_sim.bytes.constant "turnoff-pending %v"
      obelisk_sim.display %ctx to %stdout4(%fmt4, %ov4, %out) newline = true radix = 10 flags = [0, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      %two4 = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two4 to ^off

    ^off:
      %stdout5 = arith.constant 1 : i32
      %ov5 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %dv5 = obelisk_sim.net.read %downstream : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %bv5 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt5 = obelisk_sim.bytes.constant "off %v %v %v"
      obelisk_sim.display %ctx to %stdout5(%fmt5, %ov5, %out, %dv5, %downstream, %bv5, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      %on5 = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %r5 = obelisk_sim.time.constant 2
      %f5 = obelisk_sim.time.constant 3
      %t5 = obelisk_sim.time.constant 5
      obelisk_sim.net.mos.drive_delayed 0 = %on5 after[%r5, %f5, %t5] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 1 = %on5 after[%r5, %f5, %t5] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 2 = %on5 after[%r5, %f5, %t5] : !obelisk_sim.logic<1>
      obelisk_sim.net.mos.drive_delayed 5 = %on5 after[%r5, %f5, %t5] : !obelisk_sim.logic<1>
      %three5 = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %three5 to ^force_start

    ^force_start:
      %zero6 = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      obelisk_sim.override %source = %zero6 assign false : !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %one_tick = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %one_tick to ^release

    ^release:
      obelisk_sim.release_override %source assign false : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %four6 = obelisk_sim.time.constant 4
      obelisk_sim.suspend.delay %four6 to ^done

    ^done:
      %stdout6 = arith.constant 1 : i32
      %sv6 = obelisk_sim.net.read %source : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ov6 = obelisk_sim.net.read %out : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %rv6 = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %cv6 = obelisk_sim.net.read %chain : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %av6 = obelisk_sim.net.read %cycle_a : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %yv6 = obelisk_sim.net.read %cycle_b : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %bv6 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fmt6 = obelisk_sim.bytes.constant "force-pulse %v %v %v %v %v %v %v"
      obelisk_sim.display %ctx to %stdout6(%fmt6, %sv6, %source, %ov6, %out, %rv6, %rout, %cv6, %chain, %av6, %cycle_a, %yv6, %cycle_b, %bv6, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048, 2048, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
