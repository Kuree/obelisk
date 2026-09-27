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
  simulation.design @delayed_mos_strength_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.source"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.out"
    simulation.net.decl 2 in 0 : !simulation.logic<1> design hierarchy "top.rout"
    simulation.net.decl 3 in 0 : !simulation.logic<1> design hierarchy "top.downstream"
    simulation.net.decl 4 in 0 : !simulation.logic<1> design hierarchy "top.blocked"
    simulation.net.decl 5 in 0 : !simulation.logic<1> design hierarchy "top.source_upstream"
    simulation.net.decl 6 in 0 : !simulation.logic<1> design hierarchy "top.chain"
    simulation.net.decl 7 in 0 : !simulation.logic<1> design hierarchy "top.cycle_a"
    simulation.net.decl 8 in 0 : !simulation.logic<1> design hierarchy "top.cycle_b"
    simulation.driver.decl 0 in 0 drives 5 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    simulation.driver.decl 1 in 0 drives 5 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 5 : i32, strength1 = 5 : i32
    }
    simulation.driver.decl 2 in 0 drives 4 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    simulation.driver.decl 3 in 0 drives 7 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 5 : i32, strength1 = 5 : i32
    }
    simulation.net.pass.decl 0 in 0 1[0] to 0[0] width 1 reversed = false {
      control_group = 0 : i64, controlled = true, delayed = true,
      directed = true
    }
    simulation.net.pass.decl 1 in 0 2[0] to 0[0] width 1 reversed = false {
      control_group = 1 : i64, controlled = true, delayed = true,
      directed = true, resistive = true
    }
    simulation.net.pass.decl 2 in 0 4[0] to 0[0] width 1 reversed = false {
      control_group = 2 : i64, controlled = true, delayed = true,
      directed = true
    }
    simulation.net.pass.decl 3 in 0 1[0] to 3[0] width 1 reversed = false {
      resistive = true
    }
    simulation.net.pass.decl 4 in 0 0[0] to 5[0] width 1 reversed = false
    simulation.net.pass.decl 5 in 0 6[0] to 1[0] width 1 reversed = false {
      control_group = 5 : i64, controlled = true, delayed = true,
      directed = true
    }
    simulation.net.pass.decl 6 in 0 8[0] to 7[0] width 1 reversed = false {
      control_group = 6 : i64, controlled = true, delayed = true,
      directed = true
    }
    simulation.net.pass.decl 7 in 0 7[0] to 8[0] width 1 reversed = false {
      control_group = 7 : i64, controlled = true, delayed = true,
      directed = true
    }
    simulation.code_unit.decl 9991000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9991001 in 0 initial hierarchy "top.initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9991000 : i64,
                    simulation.lowered} {
      %supply = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %pull = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %block = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %cycle_driver = simulation.context.driver %ctx[3] : !simulation.driver<!simulation.logic<1>>
      %source = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %out = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %rout = simulation.context.net %ctx[2] : !simulation.net<!simulation.logic<1>>
      %downstream = simulation.context.net %ctx[3] : !simulation.net<!simulation.logic<1>>
      %blocked = simulation.context.net %ctx[4] : !simulation.net<!simulation.logic<1>>
      %chain = simulation.context.net %ctx[6] : !simulation.net<!simulation.logic<1>>
      %cycle_a = simulation.context.net %ctx[7] : !simulation.net<!simulation.logic<1>>
      %cycle_b = simulation.context.net %ctx[8] : !simulation.net<!simulation.logic<1>>
      %process = simulation.spawn @initial(%ctx, %supply, %pull, %block,
          %cycle_driver, %source, %out, %rout, %downstream, %blocked, %chain,
          %cycle_a, %cycle_b) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %supply: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %pull: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64},
        %block: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64},
        %cycle_driver: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 3 : i64},
        %source: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %out: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64},
        %rout: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64},
        %downstream: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64},
        %blocked: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 4 : i64},
        %chain: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 6 : i64},
        %cycle_a: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 7 : i64},
        %cycle_b: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 8 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9991001 : i64,
                    simulation.lowered} {
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %rise = simulation.time.constant 2
      %fall = simulation.time.constant 3
      %turnoff = simulation.time.constant 5
      simulation.driver.drive %supply = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %pull = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %block = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %cycle_driver = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.net.mos.drive_delayed 0 = %x after[%rise, %fall, %turnoff] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 1 = %x after[%rise, %fall, %turnoff] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 2 = %x after[%rise, %fall, %turnoff] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 5 = %x after[%rise, %fall, %turnoff] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 6 = %one after[%rise, %fall, %turnoff] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 7 = %one after[%rise, %fall, %turnoff] : !simulation.logic<1>
      %three = simulation.time.constant 3
      simulation.suspend.delay %three to ^initial_x

    ^initial_x:
      %stdout0 = arith.constant 1 : i32
      %ov0 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %rv0 = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %bv0 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt0 = simulation.bytes.constant "initial-x %v %v %v"
      simulation.display %ctx to %stdout0(%fmt0, %ov0, %out, %rv0, %rout, %bv0, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      %one0 = simulation.logic.constant true, false : !simulation.logic<1>
      %r0 = simulation.time.constant 2
      %f0 = simulation.time.constant 3
      %t0 = simulation.time.constant 5
      simulation.net.mos.drive_delayed 0 = %one0 after[%r0, %f0, %t0] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 1 = %one0 after[%r0, %f0, %t0] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 2 = %one0 after[%r0, %f0, %t0] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 5 = %one0 after[%r0, %f0, %t0] : !simulation.logic<1>
      %wait0 = simulation.time.constant 3
      simulation.suspend.delay %wait0 to ^on

    ^on:
      %stdout1 = arith.constant 1 : i32
      %ov1 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %rv1 = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %dv1 = simulation.net.read %downstream : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %bv1 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt1 = simulation.bytes.constant "on %v %v %v %v"
      simulation.display %ctx to %stdout1(%fmt1, %ov1, %out, %rv1, %rout, %dv1, %downstream, %bv1, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      %z1 = simulation.logic.constant true, true : !simulation.logic<1>
      simulation.driver.drive %supply = %z1 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %wait1 = simulation.time.constant 3
      simulation.suspend.delay %wait1 to ^strength_only

    ^strength_only:
      %stdout2 = arith.constant 1 : i32
      %ov2 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %rv2 = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %dv2 = simulation.net.read %downstream : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %bv2 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt2 = simulation.bytes.constant "strength-only %v %v %v %v"
      simulation.display %ctx to %stdout2(%fmt2, %ov2, %out, %rv2, %rout, %dv2, %downstream, %bv2, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      %zero2 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.driver.drive %pull = %zero2 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %pulse = simulation.time.constant 1
      simulation.suspend.delay %pulse to ^cancel

    ^cancel:
      %one3 = simulation.logic.constant true, false : !simulation.logic<1>
      simulation.driver.drive %pull = %one3 : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %wait3 = simulation.time.constant 4
      simulation.suspend.delay %wait3 to ^rejected

    ^rejected:
      %stdout3 = arith.constant 1 : i32
      %ov3 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %dv3 = simulation.net.read %downstream : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %cv3 = simulation.net.read %chain : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %bv3 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt3 = simulation.bytes.constant "rejected %v %v %v %v"
      simulation.display %ctx to %stdout3(%fmt3, %ov3, %out, %dv3, %downstream, %cv3, %chain, %bv3, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      %off3 = simulation.logic.constant false, false : !simulation.logic<1>
      %r3 = simulation.time.constant 2
      %f3 = simulation.time.constant 3
      %t3 = simulation.time.constant 5
      simulation.net.mos.drive_delayed 0 = %off3 after[%r3, %f3, %t3] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 1 = %off3 after[%r3, %f3, %t3] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 2 = %off3 after[%r3, %f3, %t3] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 5 = %off3 after[%r3, %f3, %t3] : !simulation.logic<1>
      %four3 = simulation.time.constant 4
      simulation.suspend.delay %four3 to ^turnoff_pending

    ^turnoff_pending:
      %stdout4 = arith.constant 1 : i32
      %ov4 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt4 = simulation.bytes.constant "turnoff-pending %v"
      simulation.display %ctx to %stdout4(%fmt4, %ov4, %out) newline = true radix = <decimal> flags = [0, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      %two4 = simulation.time.constant 2
      simulation.suspend.delay %two4 to ^off

    ^off:
      %stdout5 = arith.constant 1 : i32
      %ov5 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %dv5 = simulation.net.read %downstream : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %bv5 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt5 = simulation.bytes.constant "off %v %v %v"
      simulation.display %ctx to %stdout5(%fmt5, %ov5, %out, %dv5, %downstream, %bv5, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      %on5 = simulation.logic.constant true, false : !simulation.logic<1>
      %r5 = simulation.time.constant 2
      %f5 = simulation.time.constant 3
      %t5 = simulation.time.constant 5
      simulation.net.mos.drive_delayed 0 = %on5 after[%r5, %f5, %t5] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 1 = %on5 after[%r5, %f5, %t5] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 2 = %on5 after[%r5, %f5, %t5] : !simulation.logic<1>
      simulation.net.mos.drive_delayed 5 = %on5 after[%r5, %f5, %t5] : !simulation.logic<1>
      %three5 = simulation.time.constant 3
      simulation.suspend.delay %three5 to ^force_start

    ^force_start:
      %zero6 = simulation.logic.constant false, false : !simulation.logic<1>
      simulation.override %source = %zero6 assign false : !simulation.net<!simulation.logic<1>>, !simulation.logic<1>
      %one_tick = simulation.time.constant 1
      simulation.suspend.delay %one_tick to ^release

    ^release:
      simulation.release_override %source assign false : !simulation.net<!simulation.logic<1>>
      %four6 = simulation.time.constant 4
      simulation.suspend.delay %four6 to ^done

    ^done:
      %stdout6 = arith.constant 1 : i32
      %sv6 = simulation.net.read %source : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %ov6 = simulation.net.read %out : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %rv6 = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %cv6 = simulation.net.read %chain : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %av6 = simulation.net.read %cycle_a : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %yv6 = simulation.net.read %cycle_b : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %bv6 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fmt6 = simulation.bytes.constant "force-pulse %v %v %v %v %v %v %v"
      simulation.display %ctx to %stdout6(%fmt6, %sv6, %source, %ov6, %out, %rv6, %rout, %cv6, %chain, %av6, %cycle_a, %yv6, %cycle_b, %bv6, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048, 2048, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      simulation.return
    }
  }
}
