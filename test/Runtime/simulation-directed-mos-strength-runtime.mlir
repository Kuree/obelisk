// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' \
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
  obelisk_sim.design @directed_mos_strength_runtime {
    obelisk_sim.scope.decl 0 hierarchy "top"
    obelisk_sim.net.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "top.source"
    obelisk_sim.net.decl 1 in 0 : !obelisk_sim.logic<1> design hierarchy "top.nout"
    obelisk_sim.net.decl 2 in 0 : !obelisk_sim.logic<1> design hierarchy "top.rout"
    obelisk_sim.net.decl 3 in 0 : !obelisk_sim.logic<1> design hierarchy "top.blocked"
    obelisk_sim.driver.decl 0 in 0 drives 0 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    obelisk_sim.driver.decl 1 in 0 drives 0 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 5 : i32, strength1 = 5 : i32
    }
    obelisk_sim.driver.decl 2 in 0 drives 3 : !obelisk_sim.logic<1> design {
      driven_low = 0 : i64, driven_width = 1 : i64,
      strength0 = 7 : i32, strength1 = 7 : i32
    }
    obelisk_sim.net.pass.decl 0 in 0 1[0] to 0[0] width 1 reversed = false {
      controlled = true, directed = true
    }
    obelisk_sim.net.pass.decl 1 in 0 2[0] to 0[0] width 1 reversed = false {
      controlled = true, directed = true, resistive = true
    }
    obelisk_sim.net.pass.decl 2 in 0 3[0] to 0[0] width 1 reversed = false {
      controlled = true, directed = true
    }
    obelisk_sim.code_unit.decl 9960000 in 0 root_initializer hierarchy "top.root"
    obelisk_sim.code_unit.decl 9960001 in 0 initial hierarchy "top.check"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9960000 : i64} {
      %supply = obelisk_sim.context.driver %ctx[0] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %pull = obelisk_sim.context.driver %ctx[1] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %block = obelisk_sim.context.driver %ctx[2] : !obelisk_sim.driver<!obelisk_sim.logic<1>>
      %source = obelisk_sim.context.net %ctx[0] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %nout = obelisk_sim.context.net %ctx[1] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %rout = obelisk_sim.context.net %ctx[2] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %blocked = obelisk_sim.context.net %ctx[3] : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %process = obelisk_sim.spawn @check(%ctx, %supply, %pull, %block, %source, %nout, %rout, %blocked) :
          !obelisk_sim.context, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.driver<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>>,
          !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.net<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %supply: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %pull: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %block: !obelisk_sim.driver<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 5 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %source: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 0 : i64},
        %nout: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %rout: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %blocked: !obelisk_sim.net<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 4 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9960001 : i64} {
      %one = obelisk_sim.logic.constant true, false : !obelisk_sim.logic<1>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      %z = obelisk_sim.logic.constant true, true : !obelisk_sim.logic<1>
      %stdout = arith.constant 1 : i32
      obelisk_sim.driver.drive %supply = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %pull = %one : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.driver.drive %block = %zero : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control 0 = %one : !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control 1 = %one : !obelisk_sim.logic<1>
      obelisk_sim.net.pass.control 2 = %one : !obelisk_sim.logic<1>
      %s0 = obelisk_sim.net.read %source : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n0 = obelisk_sim.net.read %nout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %r0 = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %b0 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f0 = obelisk_sim.bytes.constant "supply %v %v %v %v"
      obelisk_sim.display %ctx to %stdout(%f0, %s0, %source, %n0, %nout, %r0, %rout, %b0, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.driver.drive %supply = %z : !obelisk_sim.driver<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %s1 = obelisk_sim.net.read %source : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %n1 = obelisk_sim.net.read %nout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %r1 = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %b1 = obelisk_sim.net.read %blocked : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %f1 = obelisk_sim.bytes.constant "strength-only %v %v %v %v"
      obelisk_sim.display %ctx to %stdout(%f1, %s1, %source, %n1, %nout, %r1, %rout, %b1, %blocked) newline = true radix = 10 flags = [0, 2048, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>

      obelisk_sim.net.pass.control 0 = %x : !obelisk_sim.logic<1>
      %nx = obelisk_sim.net.read %nout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fx = obelisk_sim.bytes.constant "uncertain %v"
      obelisk_sim.display %ctx to %stdout(%fx, %nx, %nout) newline = true radix = 10 flags = [0, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.net.pass.control 0 = %one : !obelisk_sim.logic<1>

      obelisk_sim.override %source = %zero assign false : !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>
      %sf = obelisk_sim.net.read %source : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %nf = obelisk_sim.net.read %nout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %rf = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ff = obelisk_sim.bytes.constant "forced %v %v %v"
      obelisk_sim.display %ctx to %stdout(%ff, %sf, %source, %nf, %nout, %rf, %rout) newline = true radix = 10 flags = [0, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.release_override %source assign false : !obelisk_sim.net<!obelisk_sim.logic<1>>
      %sr = obelisk_sim.net.read %source : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %nr = obelisk_sim.net.read %nout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %rr = obelisk_sim.net.read %rout : !obelisk_sim.net<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %fr = obelisk_sim.bytes.constant "released %v %v %v"
      obelisk_sim.display %ctx to %stdout(%fr, %sr, %source, %nr, %nout, %rr, %rout) newline = true radix = 10 flags = [0, 2048, 2048, 2048] : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>, !obelisk_sim.logic<1>, !obelisk_sim.net<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }
  }
}
