// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a \
// RUN:   %native_support/libc++.a %native_support/libc++abi.a \
// RUN:   %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s

// IEEE 1800-2017 9.2.2.2 defers the automatic time-zero activation of an
// always_comb process until after every initial process has started. The
// scheduler must therefore run the initial activation far enough to
// observe the default X value before executing the always_comb store.
// CHECK: x
// CHECK-NEXT: 0

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @deferred_comb_startup {
    obelisk_sim.scope.decl 0 hierarchy "deferred_comb_startup"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
        hierarchy "deferred_comb_startup.value"
    obelisk_sim.code_unit.decl 9900000 in 0 root_initializer
        hierarchy "deferred_comb_startup.root"
    obelisk_sim.code_unit.decl 9900001 in 0 always_comb
        hierarchy "deferred_comb_startup.comb"
    obelisk_sim.code_unit.decl 9900002 in 0 initial
        hierarchy "deferred_comb_startup.initial"
    obelisk_sim.code_unit.decl 9900003 in 0 initial
        hierarchy "deferred_comb_startup.reactive_initial"

    obelisk_sim.func @__obelisk_root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9900000 : i64} {
      %value = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %initial = obelisk_sim.spawn @initial(%ctx, %value) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %reactive = obelisk_sim.spawn @reactive_initial(%ctx, %value) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %comb = obelisk_sim.spawn @comb(%ctx, %value) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func private @comb(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 9900001 : i64} {
      %zero = obelisk_sim.logic.constant false, false :
          !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %value {obelisk_sim.continuous_store} :
          !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.return
    }

    obelisk_sim.func private @initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9900002 : i64} {
      %loaded = obelisk_sim.ref.load %value :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format = obelisk_sim.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %loaded)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>
      obelisk_sim.return
    }

    obelisk_sim.func private @reactive_initial(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9900003 : i64,
                    domain = 1 : i32, home_region = 10 : i32} {
      %loaded = obelisk_sim.ref.load %value :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %format = obelisk_sim.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %loaded)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<1>
      obelisk_sim.return
    }
  }
}
