// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph))' -o %t.graph.mlir
// RUN: FileCheck %s --check-prefix=GRAPH < %t.graph.mlir
// RUN: obelisk-opt %t.graph.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' %t.ll | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe | FileCheck %s --check-prefix=RESULT
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o3.o
// RUN: %llvm_dist/bin/clang++ %t.o3.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o3.exe
// RUN: %t.o3.exe | FileCheck %s --check-prefix=RESULT
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s --check-prefix=RESULT

// The initial producer has a later ID than its combinational consumer.
// Sampling in later time slots is race-free regardless of startup ordering.
// The graph must be acyclic as well as producing the correct settled values.
// GRAPH: compute_graph =
// GRAPH-NOT: schedule = convergence
// GRAPH-NOT: schedule = control_loop
// RESULT: settled 6
// RESULT-NEXT: settled 10
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu"
} {
  obelisk_sim.design @activation_order {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "consumer"
    obelisk_sim.code_unit.decl 3 in 0 initial hierarchy "driver"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<8> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %consumer = obelisk_sim.spawn @a_consumer(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %producer = obelisk_sim.spawn @z_driver(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @a_consumer(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      cf.br ^body
    ^body:
      %old = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %one = obelisk_sim.logic.constant 1 : i8, 0 : i8 : !obelisk_sim.logic<8>
      %next = obelisk_sim.logic.binary add %old, %one : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %next to %output : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      obelisk_sim.suspend.change %input to ^body : !obelisk_sim.ref<!obelisk_sim.logic<8>>
    }
    obelisk_sim.func @z_driver(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 3 : i64} {
      %input = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %output = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %five = obelisk_sim.logic.constant 5 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %five to %input : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^second
    ^second:
      %first = obelisk_sim.ref.load %output : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %fmt = obelisk_sim.bytes.constant "settled %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %first) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<8>
      %nine = obelisk_sim.logic.constant 9 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.ref.store %nine to %input : !obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %again = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %again to ^done
    ^done:
      %last = obelisk_sim.ref.load %output : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      %lastfmt = obelisk_sim.bytes.constant "settled %0d"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%lastfmt, %last) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<8>
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
  }
}
