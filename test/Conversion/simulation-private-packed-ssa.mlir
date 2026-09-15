// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' -o %t.off
// RUN: FileCheck %s --check-prefix=OFF < %t.off
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=READ
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph{vpi=full},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-compute-fusion))' | FileCheck %s --check-prefix=FULL
// RUN: sed 's/obelisk.native_scheduler = 3 : i32/obelisk.native_scheduler = 0 : i32/' %t.off > %t.native.mlir
// RUN: obelisk-opt %t.native.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode,convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir > %t.ll
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O0>' %t.ll | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o0.o
// RUN: %llvm_dist/bin/clang++ %t.o0.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o0.exe
// RUN: %t.o0.exe | FileCheck %s --check-prefix=RESULT
// RUN: %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' %t.ll | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o3.o
// RUN: %llvm_dist/bin/clang++ %t.o3.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.o3.exe
// RUN: %t.o3.exe | FileCheck %s --check-prefix=RESULT
// RUN: sed 's/obelisk.native_scheduler = 3 : i32/obelisk.native_scheduler = 0 : i32/' %s > %t.bytecode.mlir
// RUN: obelisk-opt %t.bytecode.mlir --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph),encode-obelisk-sim-to-bytecode{require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' | mlir-translate --mlir-to-llvmir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe | FileCheck %s --check-prefix=RESULT

// Forward whole values and nested packed views without converting their value
// domain. Two unknown bits straddle the native word boundary: one X and one Z.
// The bytecode reference is encoded from the original storage-based program.
// The dynamic spawn test uses the automatic event-loop policy after checking
// the promotion pass; its activation multiplicity cannot use an AOT plan.
// RESULT: private SSA 1 1 1 1
// RESULT-NEXT: suspended 2
// RESULT-NEXT: suspended 2
!lanes = !obelisk_sim.packed_array<9 : 8 x !obelisk_sim.logic<65>>
!record = !obelisk_sim.packed_struct<[
  #obelisk_sim.field<name = "lanes", type = !lanes, ordinal = 0, packedOffset = 4>,
  #obelisk_sim.field<name = "tag", type = i4, ordinal = 1, packedOffset = 0>
]>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @private_packed_ssa {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 function hierarchy "whole"
    obelisk_sim.code_unit.decl 3 in 0 function hierarchy "wide_bits"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "packed"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "read_before_write"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "dynamic"
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "recursive"
    obelisk_sim.code_unit.decl 8 in 0 initial hierarchy "drive"
    obelisk_sim.code_unit.decl 9 in 0 initial hierarchy "suspended"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<130> static
    obelisk_sim.storage.decl 1 in 0 : i130 static
    obelisk_sim.storage.decl 2 in 0 : !record static
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<130> static
    obelisk_sim.storage.decl 4 in 0 : !obelisk_sim.logic<130> static
    obelisk_sim.storage.decl 5 in 0 : !obelisk_sim.logic<130> static
    obelisk_sim.storage.decl 6 in 0 : !obelisk_sim.logic<130> static

    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %xz = obelisk_sim.logic.constant 9223372036854775808 : i130, 27670116110564327424 : i130 : !obelisk_sim.logic<130>
      %whole = obelisk_sim.call @whole(%ctx, %xz) : (!obelisk_sim.context, !obelisk_sim.logic<130>) -> !obelisk_sim.logic<130>
      %whole_ok = obelisk_sim.logic.compare case_eq %whole, %xz : (!obelisk_sim.logic<130>, !obelisk_sim.logic<130>) -> i1
      %bits = arith.constant 680564733841876926926749214863536422917 : i130
      %wide = obelisk_sim.call @wide_bits(%ctx, %bits) : (!obelisk_sim.context, i130) -> i130
      %wide_ok = arith.cmpi eq, %wide, %bits : i130
      %lane = obelisk_sim.logic.constant 9223372036854775808 : i65, 27670116110564327424 : i65 : !obelisk_sim.logic<65>
      %other = obelisk_sim.logic.constant 7 : i65, 0 : i65 : !obelisk_sim.logic<65>
      %lanes = obelisk_sim.aggregate.construct %other, %lane : (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> !lanes
      %tag = arith.constant 11 : i4
      %record = obelisk_sim.aggregate.construct %lanes, %tag : (!lanes, i4) -> !record
      %packed_ok = obelisk_sim.call @packed(%ctx, %record, %lane, %tag) : (!obelisk_sim.context, !record, !obelisk_sim.logic<65>, i4) -> i1
      %again = arith.constant true
      %recursive = obelisk_sim.call @recursive(%ctx, %xz, %again) : (!obelisk_sim.context, !obelisk_sim.logic<130>, i1) -> !obelisk_sim.logic<130>
      %zero = obelisk_sim.logic.constant 0 : i130, 0 : i130 : !obelisk_sim.logic<130>
      %recursive_ok = obelisk_sim.logic.compare case_eq %recursive, %zero : (!obelisk_sim.logic<130>, !obelisk_sim.logic<130>) -> i1
      %fmt = obelisk_sim.bytes.constant "private SSA %0d %0d %0d %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %whole_ok, %wide_ok, %packed_ok, %recursive_ok) newline = true radix = 10 flags = [0, 0, 0, 0, 0] : !obelisk_sim.bytes, i1, i1, i1, i1
      %driver = obelisk_sim.spawn @drive(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      obelisk_sim.return
    }

    // OFF-LABEL: obelisk_sim.func private @whole(
    // OFF-NOT: obelisk_sim.ref.
    // OFF: obelisk_sim.return %arg1
    // READ-LABEL: obelisk_sim.func private @whole(
    // READ: obelisk_sim.ref.store {{.*}}obelisk.eval.discardable_store
    // READ-NOT: obelisk_sim.ref.load
    // READ: obelisk_sim.return %arg1
    // FULL-LABEL: obelisk_sim.func private @whole(
    // FULL: obelisk_sim.ref.store
    // FULL: obelisk_sim.ref.load
    obelisk_sim.func private @whole(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<130> {obelisk_sim.capture_kind = 1 : i32}) -> !obelisk_sim.logic<130>
        attributes {entry_kind = 8 : i32, code_unit_id = 2 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<130>>
      obelisk_sim.ref.store %value to %tmp : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.br ^read
    ^read:
      %loaded = obelisk_sim.ref.load %tmp : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.logic<130>
      obelisk_sim.return %loaded : !obelisk_sim.logic<130>
    }

    // OFF-LABEL: obelisk_sim.func private @wide_bits(
    // OFF-NOT: obelisk_sim.ref.
    // OFF: obelisk_sim.return %arg1
    obelisk_sim.func private @wide_bits(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: i130 {obelisk_sim.capture_kind = 1 : i32}) -> i130
        attributes {entry_kind = 8 : i32, code_unit_id = 3 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<i130>
      obelisk_sim.ref.store %value to %tmp : i130, !obelisk_sim.ref<i130>
      %loaded = obelisk_sim.ref.load %tmp : !obelisk_sim.ref<i130> -> i130
      obelisk_sim.return %loaded : i130
    }

    // OFF-LABEL: obelisk_sim.func private @packed(
    // OFF-NOT: obelisk_sim.ref.
    // OFF: %[[LANES:.*]] = obelisk_sim.aggregate.extract %arg1[0]
    // OFF-NEXT: %[[LANE:.*]] = obelisk_sim.aggregate.extract %[[LANES]][1]
    // OFF-NEXT: %[[TAG:.*]] = obelisk_sim.aggregate.extract %arg1[1]
    // OFF: obelisk_sim.logic.compare case_eq %[[LANE]], %arg2
    // OFF: arith.cmpi eq, %[[TAG]], %arg3
    // OFF-NOT: obelisk_sim.ref.
    // OFF: obelisk_sim.return
    // READ-LABEL: obelisk_sim.func private @packed(
    // READ: obelisk_sim.ref.store {{.*}}obelisk.eval.discardable_store
    // READ-NOT: obelisk_sim.ref.load
    // READ: obelisk_sim.aggregate.extract
    // READ-NOT: obelisk_sim.ref.load
    // READ: obelisk_sim.return
    obelisk_sim.func private @packed(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !record {obelisk_sim.capture_kind = 1 : i32},
        %expected: !obelisk_sim.logic<65> {obelisk_sim.capture_kind = 1 : i32},
        %tag: i4 {obelisk_sim.capture_kind = 1 : i32}) -> i1
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!record>
      obelisk_sim.ref.store %value to %tmp : !record, !obelisk_sim.ref<!record>
      %lanes = obelisk_sim.ref.subelement %tmp[[0]] : !obelisk_sim.ref<!record> -> !obelisk_sim.ref<!lanes>
      %lane = obelisk_sim.ref.subelement %lanes[[1]] : !obelisk_sim.ref<!lanes> -> !obelisk_sim.ref<!obelisk_sim.logic<65>>
      %tag_ref = obelisk_sim.ref.subelement %tmp[[1]] : !obelisk_sim.ref<!record> -> !obelisk_sim.ref<i4>
      %loaded = obelisk_sim.ref.load %lane : !obelisk_sim.ref<!obelisk_sim.logic<65>> -> !obelisk_sim.logic<65>
      %loaded_tag = obelisk_sim.ref.load %tag_ref : !obelisk_sim.ref<i4> -> i4
      %same = obelisk_sim.logic.compare case_eq %loaded, %expected : (!obelisk_sim.logic<65>, !obelisk_sim.logic<65>) -> i1
      %tag_ok = arith.cmpi eq, %loaded_tag, %tag : i4
      %result = arith.andi %same, %tag_ok : i1
      obelisk_sim.return %result : i1
    }

    // OFF-LABEL: obelisk_sim.func private @read_before_write(
    // OFF: obelisk_sim.ref.load
    // OFF: obelisk_sim.ref.store
    // OFF: obelisk_sim.return
    obelisk_sim.func private @read_before_write(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<130> {obelisk_sim.capture_kind = 1 : i32}) -> !obelisk_sim.logic<130>
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<130>>
      %old = obelisk_sim.ref.load %tmp : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.logic<130>
      obelisk_sim.ref.store %value to %tmp : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      obelisk_sim.return %old : !obelisk_sim.logic<130>
    }

    // OFF-LABEL: obelisk_sim.func private @dynamic(
    // OFF: obelisk_sim.ref.store
    // OFF: obelisk_sim.ref.dyn_extract
    // OFF: obelisk_sim.ref.load
    obelisk_sim.func private @dynamic(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<130> {obelisk_sim.capture_kind = 1 : i32},
        %index: i32 {obelisk_sim.capture_kind = 1 : i32}) -> !obelisk_sim.logic<8>
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<!obelisk_sim.logic<130>>
      obelisk_sim.ref.store %value to %tmp : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      %part = obelisk_sim.ref.dyn_extract %tmp from %index : (!obelisk_sim.ref<!obelisk_sim.logic<130>>, i32) -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %loaded = obelisk_sim.ref.load %part : !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.return %loaded : !obelisk_sim.logic<8>
    }

    // Reentry writes the same static root through the same accessor. The
    // outer return must see the inner value, despite the dominating store.
    // OFF-LABEL: obelisk_sim.func private @recursive(
    // OFF: obelisk_sim.ref.store
    // OFF: obelisk_sim.call @recursive
    // OFF: obelisk_sim.ref.load
    obelisk_sim.func private @recursive(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<130> {obelisk_sim.capture_kind = 1 : i32},
        %again: i1 {obelisk_sim.capture_kind = 1 : i32}) -> !obelisk_sim.logic<130>
        attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[5] : !obelisk_sim.ref<!obelisk_sim.logic<130>>
      obelisk_sim.ref.store %value to %tmp : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      cf.cond_br %again, ^reenter, ^done
    ^reenter:
      %zero = obelisk_sim.logic.constant 0 : i130, 0 : i130 : !obelisk_sim.logic<130>
      %stop = arith.constant false
      %ignored = obelisk_sim.call @recursive(%ctx, %zero, %stop) : (!obelisk_sim.context, !obelisk_sim.logic<130>, i1) -> !obelisk_sim.logic<130>
      cf.br ^done
    ^done:
      %loaded = obelisk_sim.ref.load %tmp : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.logic<130>
      obelisk_sim.return %loaded : !obelisk_sim.logic<130>
    }

    // Two invocations share the same static root. Stores occur at distinct
    // times (0 and 1), and both reads occur after the second store (2 and 3).
    // This is race-free and must preserve the update across each suspension.
    obelisk_sim.func private @drive(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 8 : i64} {
      %one = obelisk_sim.logic.constant 1 : i130, 0 : i130 : !obelisk_sim.logic<130>
      %first = obelisk_sim.spawn @suspended(%ctx, %one) : !obelisk_sim.context, !obelisk_sim.logic<130> -> !obelisk_sim.process
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^second
    ^second:
      %two = obelisk_sim.logic.constant 2 : i130, 0 : i130 : !obelisk_sim.logic<130>
      %second = obelisk_sim.spawn @suspended(%ctx, %two) : !obelisk_sim.context, !obelisk_sim.logic<130> -> !obelisk_sim.process
      obelisk_sim.return
    }

    // OFF-LABEL: obelisk_sim.func private @suspended(
    // OFF: obelisk_sim.ref.store
    // OFF: obelisk_sim.suspend.delay
    // OFF: obelisk_sim.ref.load
    obelisk_sim.func private @suspended(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %value: !obelisk_sim.logic<130> {obelisk_sim.capture_kind = 1 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 9 : i64} {
      %tmp = obelisk_sim.context.storage %ctx[6] : !obelisk_sim.ref<!obelisk_sim.logic<130>>
      obelisk_sim.ref.store %value to %tmp : !obelisk_sim.logic<130>, !obelisk_sim.ref<!obelisk_sim.logic<130>>
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^read
    ^read:
      %loaded = obelisk_sim.ref.load %tmp : !obelisk_sim.ref<!obelisk_sim.logic<130>> -> !obelisk_sim.logic<130>
      %fmt = obelisk_sim.bytes.constant "suspended %0d"
      %channel = arith.constant 1 : i32
      obelisk_sim.display %ctx to %channel(%fmt, %loaded) newline = true radix = 10 flags = [0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<130>
      obelisk_sim.return
    }
  }
}
