// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-fuse-compute-fragments{body-fusion=true},obelisk-sim-materialize-compute-fusion))' -o %t.group.mlir
// RUN: FileCheck %s --check-prefix=GROUP < %t.group.mlir
// RUN: obelisk-opt %t.group.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' %t.ll | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe | FileCheck %s
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o3.o
// RUN: %llvm_dist/bin/clang++ %t.o3.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o3.exe
// RUN: %t.o3.exe | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bc.o
// RUN: %llvm_dist/bin/clang++ %t.bc.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bc.exe
// RUN: %t.bc.exe | FileCheck %s

// A static sensitivity SCC is not a reason to exclude all of its actors.
// Forward segments remain native groups and the shared loop handles their
// backward publication. The OR gate breaks the value-level loop at runtime.
// GROUP: obelisk_sim.spawn @__obelisk_region_kernel_
// GROUP: obelisk_sim.func private @__obelisk_region_kernel_
// GROUP-SAME: obelisk.native.region_body
// CHECK: settled 1 1 1 1

!bit = !obelisk_sim.logic<1>
!ref = !obelisk_sim.ref<!bit>
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 1 : i32
} {
  obelisk_sim.design @feedback {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : !bit design
    obelisk_sim.storage.decl 1 in 0 : !bit design
    obelisk_sim.storage.decl 2 in 0 : !bit design
    obelisk_sim.storage.decl 3 in 0 : !bit design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always_comb hierarchy "a"
    obelisk_sim.code_unit.decl 3 in 0 always_comb hierarchy "b"
    obelisk_sim.code_unit.decl 4 in 0 always_comb hierarchy "c"
    obelisk_sim.code_unit.decl 5 in 0 always_comb hierarchy "d"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "report"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %a = obelisk_sim.context.storage %ctx[0] : !ref
      %b = obelisk_sim.context.storage %ctx[1] : !ref
      %c = obelisk_sim.context.storage %ctx[2] : !ref
      %d = obelisk_sim.context.storage %ctx[3] : !ref
      %pa = obelisk_sim.spawn @a(%ctx, %d, %a) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %pb = obelisk_sim.spawn @b(%ctx, %a, %b) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %pc = obelisk_sim.spawn @c(%ctx, %b, %c) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %pd = obelisk_sim.spawn @d(%ctx, %c, %d) : !obelisk_sim.context, !ref, !ref -> !obelisk_sim.process
      %pr = obelisk_sim.spawn @report(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @a(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}, %output: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 2 : i64} {
      cf.br ^body
    ^body:
      %old = obelisk_sim.ref.load %input : !ref -> !bit
      %one = obelisk_sim.logic.constant true, false : !bit
      %next = obelisk_sim.logic.binary or %old, %one : !bit
      obelisk_sim.ref.store %next to %output : !bit, !ref
      obelisk_sim.suspend.change %input to ^body : !ref
    }
    obelisk_sim.func @b(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %output: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 3 : i64} {
      cf.br ^body
    ^body:
      %next = obelisk_sim.ref.load %input : !ref -> !bit
      obelisk_sim.ref.store %next to %output : !bit, !ref
      obelisk_sim.suspend.change %input to ^body : !ref
    }
    obelisk_sim.func @c(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %output: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 4 : i64} {
      cf.br ^body
    ^body:
      %next = obelisk_sim.ref.load %input : !ref -> !bit
      obelisk_sim.ref.store %next to %output : !bit, !ref
      obelisk_sim.suspend.change %input to ^body : !ref
    }
    obelisk_sim.func @d(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %input: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %output: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 4 : i32, code_unit_id = 5 : i64} {
      cf.br ^body
    ^body:
      %next = obelisk_sim.ref.load %input : !ref -> !bit
      obelisk_sim.ref.store %next to %output : !bit, !ref
      obelisk_sim.suspend.change %input to ^body : !ref
    }
    obelisk_sim.func @report(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^done
    ^done:
      %a = obelisk_sim.context.storage %ctx[0] : !ref
      %b = obelisk_sim.context.storage %ctx[1] : !ref
      %c = obelisk_sim.context.storage %ctx[2] : !ref
      %d = obelisk_sim.context.storage %ctx[3] : !ref
      %av = obelisk_sim.ref.load %a : !ref -> !bit
      %bv = obelisk_sim.ref.load %b : !ref -> !bit
      %cv = obelisk_sim.ref.load %c : !ref -> !bit
      %dv = obelisk_sim.ref.load %d : !ref -> !bit
      %stdout = arith.constant -2147483647 : i32
      %format = obelisk_sim.bytes.constant "settled %b %b %b %b"
      obelisk_sim.display %ctx to %stdout(%format, %av, %bv, %cv, %dv) newline = true radix = 2 flags = [0, 0, 0, 0, 0] : !obelisk_sim.bytes, !bit, !bit, !bit, !bit
      %zero = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %zero
      obelisk_sim.return
    }
  }
}
