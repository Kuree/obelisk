// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: %t.exe --execution-tier=native | FileCheck %s

// Hand-authored simulation IR tests the directed MOS topology independently
// of source parsing. A supply and pull driver initially agree on logical 1;
// releasing only the supply contribution therefore changes strength without
// changing the source's four-state value. Both destinations must republish.
// The local supply0 on `blocked` must never flow backward into `source`.
// CHECK: supply Su1 St1 Pu1 Su0
// CHECK: strength-only Pu1 Pu1 We1 Su0
// CHECK: uncertain PuH
// CHECK: forced Su0 St0 Pu0
// CHECK: released Pu1 Pu1 We1

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  simulation.design @directed_mos_strength_runtime {
    simulation.scope.decl 0 hierarchy "top"
    simulation.net.decl 0 in 0 : !simulation.logic<1> design hierarchy "top.source"
    simulation.net.decl 1 in 0 : !simulation.logic<1> design hierarchy "top.nout"
    simulation.net.decl 2 in 0 : !simulation.logic<1> design hierarchy "top.rout"
    simulation.net.decl 3 in 0 : !simulation.logic<1> design hierarchy "top.blocked"
    simulation.driver.decl 0 in 0 drives 0 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    simulation.driver.decl 1 in 0 drives 0 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 5 : i32, strength1 = 5 : i32
    }
    simulation.driver.decl 2 in 0 drives 3 : !simulation.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    simulation.net.pass.decl 0 in 0 1[0] to 0[0] width 1 reversed = false {
      controlled = true, directed = true
    }
    simulation.net.pass.decl 1 in 0 2[0] to 0[0] width 1 reversed = false {
      controlled = true, directed = true, resistive = true
    }
    simulation.net.pass.decl 2 in 0 3[0] to 0[0] width 1 reversed = false {
      controlled = true, directed = true
    }
    simulation.code_unit.decl 9960000 in 0 root_initializer hierarchy "top.root"
    simulation.code_unit.decl 9960001 in 0 initial hierarchy "top.check"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9960000 : i64} {
      %supply = simulation.context.driver %ctx[0] : !simulation.driver<!simulation.logic<1>>
      %pull = simulation.context.driver %ctx[1] : !simulation.driver<!simulation.logic<1>>
      %block = simulation.context.driver %ctx[2] : !simulation.driver<!simulation.logic<1>>
      %source = simulation.context.net %ctx[0] : !simulation.net<!simulation.logic<1>>
      %nout = simulation.context.net %ctx[1] : !simulation.net<!simulation.logic<1>>
      %rout = simulation.context.net %ctx[2] : !simulation.net<!simulation.logic<1>>
      %blocked = simulation.context.net %ctx[3] : !simulation.net<!simulation.logic<1>>
      %process = simulation.spawn @check(%ctx, %supply, %pull, %block, %source, %nout, %rout, %blocked) :
          !simulation.context, !simulation.driver<!simulation.logic<1>>,
          !simulation.driver<!simulation.logic<1>>, !simulation.driver<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>,
          !simulation.net<!simulation.logic<1>>, !simulation.net<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @check(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %supply: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 0 : i64},
        %pull: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 1 : i64},
        %block: !simulation.driver<!simulation.logic<1>> {simulation.capture_kind = 5 : i32, simulation.descriptor_id = 2 : i64},
        %source: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 0 : i64},
        %nout: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 1 : i64},
        %rout: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 2 : i64},
        %blocked: !simulation.net<!simulation.logic<1>> {simulation.capture_kind = 4 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9960001 : i64} {
      %one = simulation.logic.constant true, false : !simulation.logic<1>
      %zero = simulation.logic.constant false, false : !simulation.logic<1>
      %x = simulation.logic.constant false, true : !simulation.logic<1>
      %z = simulation.logic.constant true, true : !simulation.logic<1>
      %stdout = arith.constant 1 : i32
      simulation.driver.drive %supply = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %pull = %one : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.driver.drive %block = %zero : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      simulation.net.pass.control 0 = %one : !simulation.logic<1>
      simulation.net.pass.control 1 = %one : !simulation.logic<1>
      simulation.net.pass.control 2 = %one : !simulation.logic<1>
      %s0 = simulation.net.read %source : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n0 = simulation.net.read %nout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %r0 = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %b0 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f0 = simulation.bytes.constant "supply %v %v %v %v"
      simulation.display %ctx to %stdout(%f0, %s0, %source, %n0, %nout, %r0, %rout, %b0, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.driver.drive %supply = %z : !simulation.driver<!simulation.logic<1>>, !simulation.logic<1>
      %s1 = simulation.net.read %source : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %n1 = simulation.net.read %nout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %r1 = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %b1 = simulation.net.read %blocked : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %f1 = simulation.bytes.constant "strength-only %v %v %v %v"
      simulation.display %ctx to %stdout(%f1, %s1, %source, %n1, %nout, %r1, %rout, %b1, %blocked) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>

      simulation.net.pass.control 0 = %x : !simulation.logic<1>
      %nx = simulation.net.read %nout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fx = simulation.bytes.constant "uncertain %v"
      simulation.display %ctx to %stdout(%fx, %nx, %nout) newline = true radix = <decimal> flags = [0, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      simulation.net.pass.control 0 = %one : !simulation.logic<1>

      simulation.override %source = %zero assign false : !simulation.net<!simulation.logic<1>>, !simulation.logic<1>
      %sf = simulation.net.read %source : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %nf = simulation.net.read %nout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %rf = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %ff = simulation.bytes.constant "forced %v %v %v"
      simulation.display %ctx to %stdout(%ff, %sf, %source, %nf, %nout, %rf, %rout) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      simulation.release_override %source assign false : !simulation.net<!simulation.logic<1>>
      %sr = simulation.net.read %source : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %nr = simulation.net.read %nout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %rr = simulation.net.read %rout : !simulation.net<!simulation.logic<1>> -> !simulation.logic<1>
      %fr = simulation.bytes.constant "released %v %v %v"
      simulation.display %ctx to %stdout(%fr, %sr, %source, %nr, %nout, %rr, %rout) newline = true radix = <decimal> flags = [0, 2048, 2048, 2048] : !simulation.bytes, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>, !simulation.logic<1>, !simulation.net<!simulation.logic<1>>
      simulation.return
    }
  }
}
