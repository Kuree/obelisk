// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: env OBELISK_RT_SIGNAL_DIAGNOSTICS=1 %t.exe --execution-tier=native 2> %t.native.err | FileCheck %s
// RUN: FileCheck %s --check-prefix=DIAG < %t.native.err
// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-process-cfg),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off require-bytecode=true},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.bytecode.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.bytecode.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O2>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.bytecode.o
// RUN: %llvm_dist/bin/clang++ %t.bytecode.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.bytecode.exe
// RUN: %t.bytecode.exe --execution-tier=bytecode 2> %t.bytecode.err | FileCheck %s

// A context-free value helper followed by a termination poll must not create
// a checkpoint per activation. Real finish/fatal effects still use cold routes.
// The predicate may duplicate this helper, but must not duplicate stateful ones.
// PLAN: llvm.func @publish.__obelisk_eval_body_0.__obelisk_checkpoint_path
// PLAN: llvm.func @__obelisk_eval_dispatch_v1
// CHECK: 10000000000000000 1 0
// CHECK-NEXT: 0ffffffffffffffff 1 1
// The initial checker finishes through its ordinary timed continuation, not
// an eval-owner checkpoint. Neither clock activation may leave the evaluator.
// DIAG: aot_node_executions=8
// DIAG-SAME: aot_fallbacks=0 aot_checkpoints=0 aot_terminal_checkpoints=0
!wide = !obelisk_sim.logic<65>
!wref = !obelisk_sim.ref<!wide>
!ref = !obelisk_sim.ref<i1>
module attributes {llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128", llvm.target_triple = "x86_64-unknown-linux-gnu", obelisk.native_scheduler = 3 : i32} {
  obelisk_sim.design @pure_value_helper {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : !wide design
    obelisk_sim.storage.decl 2 in 0 : i1 design
    obelisk_sim.storage.decl 3 in 0 : i1 design
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "publish"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "change"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "posedge"
    obelisk_sim.code_unit.decl 6 in 0 initial hierarchy "check"
    obelisk_sim.code_unit.decl 7 in 0 function hierarchy "identity"
    obelisk_sim.code_unit.decl 8 in 0 function hierarchy "invert"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}) attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !ref
      %data = obelisk_sim.context.storage %ctx[1] : !wref
      %change = obelisk_sim.context.storage %ctx[2] : !ref
      %edge = obelisk_sim.context.storage %ctx[3] : !ref
      %zero = arith.constant false
      %zeros = obelisk_sim.logic.constant 0 : i65, 0 : i65 : !wide
      obelisk_sim.ref.store %zero to %clk : i1, !ref
      obelisk_sim.ref.store %zeros to %data : !wide, !wref
      obelisk_sim.ref.store %zero to %change : i1, !ref
      obelisk_sim.ref.store %zero to %edge : i1, !ref
      %a = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !ref -> !obelisk_sim.process
      %b = obelisk_sim.spawn @publish(%ctx, %clk, %data) : !obelisk_sim.context, !ref, !wref -> !obelisk_sim.process
      %c = obelisk_sim.spawn @change(%ctx, %data, %change) : !obelisk_sim.context, !wref, !ref -> !obelisk_sim.process
      %d = obelisk_sim.spawn @edge(%ctx, %data, %edge) : !obelisk_sim.context, !wref, !ref -> !obelisk_sim.process
      %e = obelisk_sim.spawn @check(%ctx, %data, %change, %edge) : !obelisk_sim.context, !wref, !ref, !ref -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !ref -> i1
      %one = arith.constant true
      %new = arith.xori %old, %one : i1
      obelisk_sim.ref.store %new to %clk : i1, !ref
      cf.br ^wait
    }
    obelisk_sim.func @publish(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %clk: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^write {site = #obelisk_sim.continuation<id = 2>} : !ref
    ^write:
      %value = obelisk_sim.ref.load %clk : !ref -> i1
      %reported = obelisk_sim.call @identity(%ctx, %value) : (!obelisk_sim.context, i1) -> i1
      %requested = obelisk_sim.termination.requested %ctx
      cf.cond_br %requested, ^terminated, ^validate
    ^terminated:
      obelisk_sim.return
    ^validate:
      %bad = arith.xori %reported, %value : i1
      cf.cond_br %bad, ^fatal, ^choose
    ^fatal:
      %verbosity = arith.constant 0 : i32
      obelisk_sim.fatal %ctx, %verbosity
      obelisk_sim.return
    ^choose:
      cf.cond_br %reported, ^high, ^low
    ^high:
      %h = obelisk_sim.logic.constant 18446744073709551616 : i65, 0 : i65 : !wide
      obelisk_sim.ref.store %h to %data : !wide, !wref
      cf.br ^wait
    ^low:
      %l = obelisk_sim.logic.constant 18446744073709551615 : i65, 0 : i65 : !wide
      obelisk_sim.ref.store %l to %data : !wide, !wref
      cf.br ^wait
    }
    obelisk_sim.func @change(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %flag: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %data to ^changed {site = #obelisk_sim.continuation<id = 3>} : !wref
    ^changed:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %flag : i1, !ref
      cf.br ^wait
    }
    obelisk_sim.func @edge(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %flag: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.any %data edges [1] to ^edge {site = #obelisk_sim.continuation<id = 4>} : !wref
    ^edge:
      %one = arith.constant true
      obelisk_sim.ref.store %one to %flag : i1, !ref
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %data: !wref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64}, %change: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 2 : i64}, %edge: !ref {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 3 : i64}) attributes {entry_kind = 1 : i32, code_unit_id = 6 : i64} {
      %format = obelisk_sim.bytes.constant "%017h %b %b"
      %stdout = arith.constant 1 : i32
      %delay = obelisk_sim.time.constant 3
      obelisk_sim.suspend.delay %delay to ^first {site = #obelisk_sim.continuation<id = 5>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^first:
      %v0 = obelisk_sim.ref.load %data : !wref -> !wide
      %c0 = obelisk_sim.ref.load %change : !ref -> i1
      %e0 = obelisk_sim.ref.load %edge : !ref -> i1
      obelisk_sim.display %ctx to %stdout(%format, %v0, %c0, %e0) newline = true radix = 10 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, !wide, i1, i1
      %two = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %two to ^second {site = #obelisk_sim.continuation<id = 6>, timing = #obelisk_sim.timing_site<id = 2, kind = calendar>}
    ^second:
      %v1 = obelisk_sim.ref.load %data : !wref -> !wide
      %c1 = obelisk_sim.ref.load %change : !ref -> i1
      %e1 = obelisk_sim.ref.load %edge : !ref -> i1
      obelisk_sim.display %ctx to %stdout(%format, %v1, %c1, %e1) newline = true radix = 10 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, !wide, i1, i1
      %status = arith.constant 0 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
    obelisk_sim.func private @identity(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i1 {obelisk_sim.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 7 : i64} {
      %inverted = obelisk_sim.call @invert(%ctx, %value) : (!obelisk_sim.context, i1) -> i1
      %result = obelisk_sim.call @invert(%ctx, %inverted) : (!obelisk_sim.context, i1) -> i1
      obelisk_sim.return %result : i1
    }
    obelisk_sim.func private @invert(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32}, %value: i1 {obelisk_sim.capture_kind = 2 : i32}) -> i1 attributes {entry_kind = 8 : i32, code_unit_id = 8 : i64} {
      %one = arith.constant true
      %result = arith.xori %value, %one : i1
      obelisk_sim.return %result : i1
    }
  }
}
