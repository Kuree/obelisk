// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' \
// RUN:   | mlir-translate --mlir-to-llvmir \
// RUN:   | %llc -filetype=obj -relocation-model=pic -o %t.o
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
  simulation.design @deferred_comb_startup {
    simulation.scope.decl 0 hierarchy "deferred_comb_startup"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
        hierarchy "deferred_comb_startup.value"
    simulation.code_unit.decl 9900000 in 0 root_initializer
        hierarchy "deferred_comb_startup.root"
    simulation.code_unit.decl 9900001 in 0 always_comb
        hierarchy "deferred_comb_startup.comb"
    simulation.code_unit.decl 9900002 in 0 initial
        hierarchy "deferred_comb_startup.initial"
    simulation.code_unit.decl 9900003 in 0 initial
        hierarchy "deferred_comb_startup.reactive_initial"

    simulation.func @__obelisk_root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 9900000 : i64} {
      %value = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %initial = simulation.spawn @initial(%ctx, %value) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %reactive = simulation.spawn @reactive_initial(%ctx, %value) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %comb = simulation.spawn @comb(%ctx, %value) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      simulation.return
    }

    simulation.func private @comb(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 4 : i32, code_unit_id = 9900001 : i64} {
      %zero = simulation.logic.constant false, false :
          !simulation.logic<1>
      simulation.ref.store %zero to %value {simulation.continuous_store} :
          !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      simulation.return
    }

    simulation.func private @initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9900002 : i64} {
      %loaded = simulation.ref.load %value :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %loaded)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<1>
      simulation.return
    }

    simulation.func private @reactive_initial(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %value: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 9900003 : i64,
                    domain = 1 : i32, home_region = 10 : i32} {
      %loaded = simulation.ref.load %value :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %format = simulation.bytes.constant "%b"
      %stdout = arith.constant 1 : i32
      simulation.display %ctx to %stdout(%format, %loaded)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<1>
      simulation.return
    }
  }
}
