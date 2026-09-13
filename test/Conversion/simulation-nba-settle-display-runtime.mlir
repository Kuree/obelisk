// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),encode-obelisk-sim-to-bytecode{vpi=off},convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.llvm.mlir
// RUN: FileCheck %s --check-prefix=PLAN < %t.llvm.mlir
// RUN: mlir-translate --mlir-to-llvmir %t.llvm.mlir | %llvm_dist/bin/opt -passes='coro-early,coro-split<reuse-storage>,coro-cleanup,default<O3>' | %llvm_dist/bin/llc -filetype=obj -relocation-model=pic -o %t.o
// RUN: %llvm_dist/bin/clang++ %t.o %native_support/libobelisk_rt.a %native_support/libc++.a %native_support/libc++abi.a %native_support/libunwind.a -nostdlib++ -lpthread -ldl -o %t.exe
// RUN: %t.exe --execution-tier=native | FileCheck %s
// RUN: %t.exe --execution-tier=bytecode | FileCheck %s
//
// IEEE 1800-2023 4.5, 4.9.4, 10.4.2, and 21.2.1:
// a clock activation stages A, A's publication stages B in a SECOND NBA
// iteration, and immediate displays must observe values before their own NBA.
// Empty-barrier bypass must not suppress the second commit, and canonical
// handover must clear B's initial X. No display may execute in a dry-run probe.
// PLAN-DAG: llvm.call @obelisk_rt_v1_eval_display
// PLAN-DAG: llvm.func @__obelisk_eval_fast_coordinator_hybrid_v1
// PLAN-DAG: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot
// CHECK: active 0 x
// CHECK-NEXT: cascade 1 x
// CHECK-NEXT: settled 2 1 1
// CHECK-NEXT: active 1 1
// CHECK-NEXT: cascade 0 1
// CHECK-NEXT: settled 4 0 0
// CHECK-NEXT: active 0 0
// CHECK-NEXT: cascade 1 0
// CHECK-NEXT: settled 6 1 1
// CHECK-NOT: {{active|cascade|settled}}

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @nba_settle {
    obelisk_sim.scope.decl 0 hierarchy "nba_settle"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "nba_settle.root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "nba_settle.clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "nba_settle.writer"
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "nba_settle.cascade"
    obelisk_sim.code_unit.decl 5 in 0 initial hierarchy "nba_settle.check"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %a = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %zero = obelisk_sim.logic.constant false, false : !obelisk_sim.logic<1>
      %x = obelisk_sim.logic.constant false, true : !obelisk_sim.logic<1>
      obelisk_sim.ref.store %zero to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %zero to %a : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      obelisk_sim.ref.store %x to %b : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %p = obelisk_sim.spawn @writer(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %q = obelisk_sim.spawn @cascade(%ctx, %a) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      %r = obelisk_sim.spawn @check(%ctx) : !obelisk_sim.context -> !obelisk_sim.process
      %c = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle {site = #obelisk_sim.continuation<id = 1>, timing = #obelisk_sim.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clk : !obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }
    obelisk_sim.func @writer(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.edge posedge %clk to ^write {site = #obelisk_sim.continuation<id = 2>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^write:
      %a = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %old = obelisk_sim.ref.load %a : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old : (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %new to %a : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %current = obelisk_sim.ref.load %a : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %downstream = obelisk_sim.ref.load %b : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %channel = arith.constant 1 : i32
      %format = obelisk_sim.bytes.constant "active %b %b"
      obelisk_sim.display %ctx to %channel(%format, %current, %downstream) newline = true radix = 2 flags = [0, 0, 0] {scope = "nba_settle.writer"} : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      cf.br ^wait
    }
    obelisk_sim.func @cascade(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %a: !obelisk_sim.ref<!obelisk_sim.logic<1>> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %a to ^write {site = #obelisk_sim.continuation<id = 3>} : !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^write:
      %b = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %new = obelisk_sim.ref.load %a : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      obelisk_sim.nba.enqueue %new to %b : (!obelisk_sim.logic<1>, !obelisk_sim.ref<!obelisk_sim.logic<1>>) -> ()
      %old = obelisk_sim.ref.load %b : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %channel = arith.constant 1 : i32
      %format = obelisk_sim.bytes.constant "cascade %b %b"
      obelisk_sim.display %ctx to %channel(%format, %new, %old) newline = true radix = 2 flags = [0, 0, 0] {scope = "nba_settle.cascade"} : !obelisk_sim.bytes, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      cf.br ^wait
    }
    obelisk_sim.func @check(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 1 : i32, code_unit_id = 5 : i64} {
      %delay = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay to ^first {site = #obelisk_sim.continuation<id = 4>, timing = #obelisk_sim.timing_site<id = 1, kind = calendar>}
    ^first:
      %a0 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b0 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %va0 = obelisk_sim.ref.load %a0 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %vb0 = obelisk_sim.ref.load %b0 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ch0 = arith.constant 1 : i32
      %now0 = obelisk_sim.time.now %ctx
      %fmt0 = obelisk_sim.bytes.constant "settled %0d %b %b"
      obelisk_sim.display %ctx to %ch0(%fmt0, %now0, %va0, %vb0) newline = true radix = 2 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      %delay0 = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay0 to ^second {site = #obelisk_sim.continuation<id = 5>, timing = #obelisk_sim.timing_site<id = 2, kind = calendar>}
    ^second:
      %a1 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b1 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %va1 = obelisk_sim.ref.load %a1 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %vb1 = obelisk_sim.ref.load %b1 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ch1 = arith.constant 1 : i32
      %now1 = obelisk_sim.time.now %ctx
      %fmt1 = obelisk_sim.bytes.constant "settled %0d %b %b"
      obelisk_sim.display %ctx to %ch1(%fmt1, %now1, %va1, %vb1) newline = true radix = 2 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      %delay1 = obelisk_sim.time.constant 2
      obelisk_sim.suspend.delay %delay1 to ^third {site = #obelisk_sim.continuation<id = 6>, timing = #obelisk_sim.timing_site<id = 3, kind = calendar>}
    ^third:
      %a2 = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %b2 = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %va2 = obelisk_sim.ref.load %a2 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %vb2 = obelisk_sim.ref.load %b2 : !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %ch2 = arith.constant 1 : i32
      %now2 = obelisk_sim.time.now %ctx
      %fmt2 = obelisk_sim.bytes.constant "settled %0d %b %b"
      obelisk_sim.display %ctx to %ch2(%fmt2, %now2, %va2, %vb2) newline = true radix = 2 flags = [0, 0, 0, 0] : !obelisk_sim.bytes, i64, !obelisk_sim.logic<1>, !obelisk_sim.logic<1>
      %status = arith.constant 1 : i32
      obelisk_sim.finish %ctx, %status
      obelisk_sim.return
    }
  }
}
