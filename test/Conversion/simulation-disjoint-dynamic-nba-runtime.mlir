// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=GUARD < %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=BARRIER < %t.llvm.mlir
// Empty dynamic slots must bypass stale address/value/unknown loads in all
// three commit variants. The runtime checks below cover independent slots,
// aliasing lanes, and negative/out-of-range/unknown indices.
// GUARD-LABEL: llvm.func {{(internal )?}}@__obelisk_aot_static_nba_commit_v1(
// GUARD: %[[ADDR0:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_
// GUARD-NEXT: %[[VALID0:.*]] = llvm.load %[[ADDR0]] {{.*}} : !llvm.ptr -> i32
// GUARD-NEXT: %[[ZERO0:.*]] = llvm.mlir.constant(0 : i32)
// GUARD-NEXT: %[[ACTIVE0:.*]] = llvm.icmp "ne" %[[VALID0]], %[[ZERO0]] : i32
// GUARD-NEXT: llvm.cond_br %[[ACTIVE0]], ^[[WORK0:bb[0-9]+]], ^[[NEXT0:bb[0-9]+]]
// GUARD: ^[[WORK0]]:
// GUARD: llvm.mlir.addressof @__obelisk_eval_nba_offset_
// GUARD: ^[[NEXT0]]:
// GUARD-NEXT: {{.*}}llvm.mlir.addressof @__obelisk_eval_nba_valid_
// GUARD-LABEL: llvm.func {{(internal )?}}@__obelisk_aot_static_nba_commit_two_state_v1(
// GUARD: %[[ADDR1:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_
// GUARD-NEXT: %[[VALID1:.*]] = llvm.load %[[ADDR1]] {{.*}} : !llvm.ptr -> i32
// GUARD-NEXT: %[[ZERO1:.*]] = llvm.mlir.constant(0 : i32)
// GUARD-NEXT: %[[ACTIVE1:.*]] = llvm.icmp "ne" %[[VALID1]], %[[ZERO1]] : i32
// GUARD-NEXT: llvm.cond_br %[[ACTIVE1]], ^[[WORK1:bb[0-9]+]], ^[[NEXT1:bb[0-9]+]]
// GUARD: ^[[WORK1]]:
// GUARD: llvm.mlir.addressof @__obelisk_eval_nba_offset_
// GUARD: ^[[NEXT1]]:
// GUARD-NEXT: {{.*}}llvm.mlir.addressof @__obelisk_eval_nba_valid_
// GUARD-LABEL: llvm.func {{(internal )?}}@__obelisk_aot_static_nba_commit_two_state_fast_v1(
// GUARD: %[[ADDR2:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_
// GUARD-NEXT: %[[VALID2:.*]] = llvm.load %[[ADDR2]] {{.*}} : !llvm.ptr -> i32
// GUARD-NEXT: %[[ZERO2:.*]] = llvm.mlir.constant(0 : i32)
// GUARD-NEXT: %[[ACTIVE2:.*]] = llvm.icmp "ne" %[[VALID2]], %[[ZERO2]] : i32
// GUARD-NEXT: llvm.cond_br %[[ACTIVE2]], ^[[WORK2:bb[0-9]+]], ^[[NEXT2:bb[0-9]+]]
// GUARD: ^[[WORK2]]:
// GUARD: llvm.mlir.addressof @__obelisk_eval_nba_offset_
// GUARD: ^[[NEXT2]]:
// GUARD-NEXT: {{.*}}llvm.mlir.addressof @__obelisk_eval_nba_valid_
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph{vpi=read},obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=read},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=GUARD < %t.read.llvm.mlir
// RUN: FileCheck %s --check-prefix=BARRIER < %t.read.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.read.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.read.o
// RUN: %llvm_dist/bin/clang++ %t.read.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.read.exe
// RUN: %t.read.exe | FileCheck %s
// Enabling read-only reflection must not remove dynamic NBA fast paths or
// weaken their empty-slot guards, even when no VPI consumer is installed.
// Two independent lanes must preserve both writes, including when their
// indices alias. Negative, out-of-range and unknown indices must write none.
// PLAN-COUNT-2: llvm.mlir.global internal @__obelisk_eval_nba_valid_
// PLAN-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// PLAN: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// CHECK: 00000055 00550000
// CHECK-NEXT: 00000055 00550000
// CHECK-NEXT: 00000055 00550000
// CHECK-NEXT: 00550055 00550000

// The coordinator must include BOTH dynamic slots in its empty-barrier test.
// Neither slot sets fixed dirty bits. Check every coordinator variant; the
// executable assertions below catch lost dynamic-only NBA publications.
// BARRIER-LABEL: llvm.func @__obelisk_eval_fast_coordinator_v1(
// BARRIER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// BARRIER: %[[D0:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// BARRIER-NEXT: %[[A0:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_0
// BARRIER-NEXT: %[[V0:.*]] = llvm.load %[[A0]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[Z0:.*]] = llvm.zext %[[V0]] : i32 to i64
// BARRIER-NEXT: %[[P0:.*]] = llvm.or %[[D0]], %[[Z0]] : i64
// BARRIER-NEXT: %[[B0:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_1
// BARRIER-NEXT: %[[W0:.*]] = llvm.load %[[B0]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[X0:.*]] = llvm.zext %[[W0]] : i32 to i64
// BARRIER-NEXT: %[[Q0:.*]] = llvm.or %[[P0]], %[[X0]] : i64
// BARRIER-NEXT: %[[ZERO0:.*]] = llvm.mlir.constant(0 : i64)
// BARRIER-NEXT: %[[EMPTY0:.*]] = llvm.icmp "eq" %[[Q0]], %[[ZERO0]] : i64
// BARRIER-NEXT: %[[OK0:.*]] = llvm.mlir.constant(0 : i32)
// BARRIER-NEXT: llvm.cond_br %[[EMPTY0]], ^[[DONE0:bb[0-9]+]](%[[OK0]] : i32), ^[[COMMIT0:bb[0-9]+]]
// BARRIER-NEXT: ^[[COMMIT0]]:
// BARRIER: ^[[DONE0]](%[[STATUS0:.*]]: i32):
// BARRIER-NEXT: llvm.return %[[STATUS0]] : i32
// BARRIER-LABEL: llvm.func @__obelisk_eval_fast_coordinator_two_state_v1(
// BARRIER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// BARRIER: %[[D1:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// BARRIER-NEXT: %[[A1:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_0
// BARRIER-NEXT: %[[V1:.*]] = llvm.load %[[A1]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[Z1:.*]] = llvm.zext %[[V1]] : i32 to i64
// BARRIER-NEXT: %[[P1:.*]] = llvm.or %[[D1]], %[[Z1]] : i64
// BARRIER-NEXT: %[[B1:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_1
// BARRIER-NEXT: %[[W1:.*]] = llvm.load %[[B1]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[X1:.*]] = llvm.zext %[[W1]] : i32 to i64
// BARRIER-NEXT: %[[Q1:.*]] = llvm.or %[[P1]], %[[X1]] : i64
// BARRIER-NEXT: %[[ZERO1:.*]] = llvm.mlir.constant(0 : i64)
// BARRIER-NEXT: %[[EMPTY1:.*]] = llvm.icmp "eq" %[[Q1]], %[[ZERO1]] : i64
// BARRIER-NEXT: %[[OK1:.*]] = llvm.mlir.constant(0 : i32)
// BARRIER-NEXT: llvm.cond_br %[[EMPTY1]], ^[[DONE1:bb[0-9]+]](%[[OK1]] : i32), ^[[COMMIT1:bb[0-9]+]]
// BARRIER-NEXT: ^[[COMMIT1]]:
// BARRIER: ^[[DONE1]](%[[STATUS1:.*]]: i32):
// BARRIER-NEXT: llvm.return %[[STATUS1]] : i32
// BARRIER-LABEL: llvm.func @__obelisk_eval_steady_two_state_coordinator_v1(
// BARRIER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// BARRIER: %[[D2:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// BARRIER-NEXT: %[[A2:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_0
// BARRIER-NEXT: %[[V2:.*]] = llvm.load %[[A2]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[Z2:.*]] = llvm.zext %[[V2]] : i32 to i64
// BARRIER-NEXT: %[[P2:.*]] = llvm.or %[[D2]], %[[Z2]] : i64
// BARRIER-NEXT: %[[B2:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_1
// BARRIER-NEXT: %[[W2:.*]] = llvm.load %[[B2]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[X2:.*]] = llvm.zext %[[W2]] : i32 to i64
// BARRIER-NEXT: %[[Q2:.*]] = llvm.or %[[P2]], %[[X2]] : i64
// BARRIER-NEXT: %[[ZERO2:.*]] = llvm.mlir.constant(0 : i64)
// BARRIER-NEXT: %[[EMPTY2:.*]] = llvm.icmp "eq" %[[Q2]], %[[ZERO2]] : i64
// BARRIER-NEXT: %[[OK2:.*]] = llvm.mlir.constant(0 : i32)
// BARRIER-NEXT: llvm.cond_br %[[EMPTY2]], ^[[DONE2:bb[0-9]+]](%[[OK2]] : i32), ^[[COMMIT2:bb[0-9]+]]
// BARRIER-NEXT: ^[[COMMIT2]]:
// BARRIER: ^[[DONE2]](%[[STATUS2:.*]]: i32):
// BARRIER-NEXT: llvm.return %[[STATUS2]] : i32
// BARRIER-LABEL: llvm.func @__obelisk_eval_fast_coordinator_hybrid_v1(
// BARRIER: llvm.mlir.addressof @__obelisk_aot_nba_dirty_roots_v1
// BARRIER: %[[D3:.*]] = llvm.load {{.*}} : !llvm.ptr -> i64
// BARRIER-NEXT: %[[A3:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_0
// BARRIER-NEXT: %[[V3:.*]] = llvm.load %[[A3]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[Z3:.*]] = llvm.zext %[[V3]] : i32 to i64
// BARRIER-NEXT: %[[P3:.*]] = llvm.or %[[D3]], %[[Z3]] : i64
// BARRIER-NEXT: %[[B3:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_valid_1
// BARRIER-NEXT: %[[W3:.*]] = llvm.load %[[B3]] {{.*}} : !llvm.ptr -> i32
// BARRIER-NEXT: %[[X3:.*]] = llvm.zext %[[W3]] : i32 to i64
// BARRIER-NEXT: %[[Q3:.*]] = llvm.or %[[P3]], %[[X3]] : i64
// BARRIER-NEXT: %[[ZERO3:.*]] = llvm.mlir.constant(0 : i64)
// BARRIER-NEXT: %[[EMPTY3:.*]] = llvm.icmp "eq" %[[Q3]], %[[ZERO3]] : i64
// BARRIER-NEXT: %[[OK3:.*]] = llvm.mlir.constant(0 : i32)
// BARRIER-NEXT: llvm.cond_br %[[EMPTY3]], ^[[DONE3:bb[0-9]+]](%[[OK3]] : i32), ^[[COMMIT3:bb[0-9]+]]
// BARRIER-NEXT: ^[[COMMIT3]]:
// BARRIER: ^[[DONE3]](%[[STATUS3:.*]]: i32):
// BARRIER-NEXT: llvm.return %[[STATUS3]] : i32

!words = !obelisk_sim.unpacked_array<0 : 31 x !obelisk_sim.logic<32>>

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @eval_disjoint_dynamic_nba_runtime {
    obelisk_sim.scope.decl 0 hierarchy "eval_disjoint_dynamic_nba_runtime"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_disjoint_dynamic_nba_runtime.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "eval_disjoint_dynamic_nba_runtime.clock_process"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "eval_disjoint_dynamic_nba_runtime.update"
    obelisk_sim.code_unit.decl 4 in 0 initial hierarchy "eval_disjoint_dynamic_nba_runtime.check"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design hierarchy "eval_disjoint_dynamic_nba_runtime.clock"
    obelisk_sim.storage.decl 1 in 0 : !words design hierarchy "eval_disjoint_dynamic_nba_runtime.data"
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design hierarchy "eval_disjoint_dynamic_nba_runtime.index"
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<64> design hierarchy "eval_disjoint_dynamic_nba_runtime.other_index"

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %data = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!words>
      %index = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %otherIndex = obelisk_sim.context.storage %ctx[3] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %zeroClock = obelisk_sim.logic.constant 0 : i1, 0 : i1 : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zeroClock to %clock : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %zeroIndex = obelisk_sim.logic.constant 0 : i64, 0 : i64 : !obelisk_sim.logic<64>
      %oneIndex = obelisk_sim.logic.constant 1 : i64, 0 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %zeroIndex to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %oneIndex to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %zeroWord = obelisk_sim.logic.constant 0 : i32, 0 : i32 : !obelisk_sim.logic<32>
      %word0 = obelisk_sim.ref.subelement %data[[0]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %word1 = obelisk_sim.ref.subelement %data[[1]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %zeroWord to %word0 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      obelisk_sim.ref.store %zeroWord to %word1 : !obelisk_sim.logic<32>, !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %check = obelisk_sim.spawn @check(%ctx, %data, %index, %otherIndex) :
          !obelisk_sim.context, !obelisk_sim.ref<!words>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>, !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.process
      %c = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %p = obelisk_sim.spawn @update(%ctx, %clock, %data, %index, %otherIndex) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!words>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.process
      obelisk_sim.return
    }

    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
          {site = #obelisk_sim.continuation<id = 1>,
           timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @update(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %data: !obelisk_sim.ref<!words>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %index: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %otherIndex: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #obelisk_sim.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %low = obelisk_sim.ref.load %index :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %element = obelisk_sim.ref.array_element %data[%low] :
          (!obelisk_sim.ref<!words>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %lo = obelisk_sim.ref.extract %element from 0 :
          !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %otherLow = obelisk_sim.ref.load %otherIndex :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %otherElement = obelisk_sim.ref.array_element %data[%otherLow] :
          (!obelisk_sim.ref<!words>, !obelisk_sim.logic<64>) ->
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %hi = obelisk_sim.ref.extract %otherElement from 16 :
          !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.logic.constant 85 : i8, 0 : i8 : !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %lo :
          (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      obelisk_sim.nba.enqueue %value to %hi :
          (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      cf.br ^wait
    }
    obelisk_sim.func @check(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %data: !obelisk_sim.ref<!words> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64},
        %index: !obelisk_sim.ref<!obelisk_sim.logic<64>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64},
        %otherIndex: !obelisk_sim.ref<!obelisk_sim.logic<64>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 1 : i32, code_unit_id = 4 : i64} {
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^separate
    ^separate:
      %word0 = obelisk_sim.ref.subelement %data[[0]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %word1 = obelisk_sim.ref.subelement %data[[1]] : !obelisk_sim.ref<!words> -> !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %first0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %first1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %format = obelisk_sim.bytes.constant "%08h %08h"
      %stdout = arith.constant 1 : i32
      obelisk_sim.display %ctx to %stdout(%format, %first0, %first1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %invalid = obelisk_sim.logic.constant 32 : i64, 0 : i64 : !obelisk_sim.logic<64>
      %negative = obelisk_sim.logic.constant -1 : i64, 0 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %invalid to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %negative to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^outside
    ^outside:
      %outside0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %outside1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %stdout(%format, %outside0, %outside1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %unknown = obelisk_sim.logic.constant 0 : i64, -1 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %unknown to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %unknown to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.suspend.delay %two to ^unknown
    ^unknown:
      %unknown0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %unknown1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %stdout(%format, %unknown0, %unknown1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %zero = obelisk_sim.logic.constant 0 : i64, 0 : i64 : !obelisk_sim.logic<64>
      obelisk_sim.ref.store %zero to %index : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.ref.store %zero to %otherIndex : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      obelisk_sim.suspend.delay %two to ^same
    ^same:
      %same0 = obelisk_sim.ref.load %word0 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      %same1 = obelisk_sim.ref.load %word1 : !obelisk_sim.ref<!obelisk_sim.logic<32>> -> !obelisk_sim.logic<32>
      obelisk_sim.display %ctx to %stdout(%format, %same0, %same1) newline = true radix = 10 flags = [0, 0, 0] : !obelisk_sim.bytes, !obelisk_sim.logic<32>, !obelisk_sim.logic<32>
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
