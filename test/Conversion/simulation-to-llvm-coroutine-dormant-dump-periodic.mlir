// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.llvm.mlir
// RUN: FileCheck %s < %t.llvm.mlir
// RUN: sed -e 's/x86_64-unknown-linux-gnu/wasm32-unknown-emscripten/g' -e 's/e-m:e-p:64:64-i64:64-n8:16:32:64-S128/e-m:e-p:32:32-i64:64-n32:64-S128/g' %t.planned.mlir > %t.wasm.mlir
// RUN: obelisk-opt %t.wasm.mlir --convert-obelisk-sim-processes-to-llvm-coroutines | FileCheck %s --check-prefix=CHECK --check-prefix=WASM

// WASM: module attributes {{.*}}llvm.data_layout = "e-m:e-p:32:32-i64:64-n32:64-S128"
// WASM-SAME: llvm.target_triple = "wasm32-unknown-emscripten"

// Optional dump operations remain cold checkpoints. Their mere presence must
// not remove the periodic Tier-1 plan; the runtime rejects it when dumping
// actually becomes active.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @optional_dump {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "optional_dump.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "optional_dump.clock"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "optional_dump.consume"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<8> design

    obelisk_sim.func @root(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = obelisk_sim.context.storage %ctx[0] :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      %data = obelisk_sim.context.storage %ctx[1] :
          !obelisk_sim.ref<!obelisk_sim.logic<32>>
      %index = obelisk_sim.context.storage %ctx[2] :
          !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %sink = obelisk_sim.context.storage %ctx[3] :
          !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %flag = obelisk_sim.ref.load %index :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %bits = obelisk_sim.logic.to_bits %flag : !obelisk_sim.logic<64> -> i64
      %zero = arith.constant 0 : i64
      %enabled = arith.cmpi ne, %bits, %zero : i64
      cf.cond_br %enabled, ^dump, ^start
    ^dump:
      %levels = arith.constant 0 : i64
      %scope = obelisk_sim.bytes.constant ""
      obelisk_sim.dump.vars %ctx, %levels, %scope :
          (!obelisk_sim.context, i64, !obelisk_sim.bytes) -> ()
      cf.br ^start
    ^start:
      %c = obelisk_sim.spawn @clock(%ctx, %clock) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>
          -> !obelisk_sim.process
      %p = obelisk_sim.spawn @consume(%ctx, %clock, %data, %index, %sink) :
          !obelisk_sim.context, !obelisk_sim.ref<!obelisk_sim.logic<1>>,
          !obelisk_sim.ref<!obelisk_sim.logic<32>>,
          !obelisk_sim.ref<!obelisk_sim.logic<64>>,
          !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.process
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
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = obelisk_sim.ref.load %clock :
          !obelisk_sim.ref<!obelisk_sim.logic<1>> -> !obelisk_sim.logic<1>
      %new = obelisk_sim.logic.unary bit_not %old :
          (!obelisk_sim.logic<1>) -> !obelisk_sim.logic<1>
      obelisk_sim.ref.store %new to %clock : !obelisk_sim.logic<1>,
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
      cf.br ^wait
    }

    obelisk_sim.func @consume(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clock: !obelisk_sim.ref<!obelisk_sim.logic<1>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 0 : i64},
        %data: !obelisk_sim.ref<!obelisk_sim.logic<32>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 1 : i64},
        %index: !obelisk_sim.ref<!obelisk_sim.logic<64>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 2 : i64},
        %sink: !obelisk_sim.ref<!obelisk_sim.logic<8>>
            {obelisk_sim.capture_kind = 3 : i32,
             obelisk_sim.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !obelisk_sim.ref<!obelisk_sim.logic<1>>
    ^resume:
      %low = obelisk_sim.ref.load %index :
          !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      %slice = obelisk_sim.ref.dyn_extract %data from %low :
          (!obelisk_sim.ref<!obelisk_sim.logic<32>>,
           !obelisk_sim.logic<64>) -> !obelisk_sim.ref<!obelisk_sim.logic<8>>
      %value = obelisk_sim.ref.load %slice :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.nba.enqueue %value to %sink :
          (!obelisk_sim.logic<8>, !obelisk_sim.ref<!obelisk_sim.logic<8>>) -> ()
      %bits = obelisk_sim.logic.to_bits %low : !obelisk_sim.logic<64> -> i64
      %zero = arith.constant 0 : i64
      %show = arith.cmpi ne, %bits, %zero : i64
      cf.cond_br %show, ^display, ^wait
    ^display:
      %format = obelisk_sim.bytes.constant "value=%h"
      %stdout = arith.constant 1 : i32
      %previous = obelisk_sim.ref.load %sink :
          !obelisk_sim.ref<!obelisk_sim.logic<8>> -> !obelisk_sim.logic<8>
      obelisk_sim.display %ctx to %stdout(%format, %previous)
          newline = true radix = 10 flags = [0, 0] :
          !obelisk_sim.bytes, !obelisk_sim.logic<8>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CHECK: %[[CONTROL:.*]] = llvm.alloca {{.*}} x !llvm.struct<(ptr, ptr, i64)>
// CHECK: llvm.call @obelisk_rt_v1_scheduler_prepare_periodic_aot({{.*}}, %[[CONTROL]])
// Pointer-sized control fields must use typed struct indices, not the host's
// byte offsets. The extra RUN exercises this exact path with wasm32 layout.
// CHECK: %[[TERMINATION:.*]] = llvm.getelementptr %[[CONTROL]][0, 1] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(ptr, ptr, i64)>
// CHECK-NEXT: %[[TERMINATION_PTR:.*]] = llvm.load %[[TERMINATION]] : !llvm.ptr -> !llvm.ptr
// CHECK: %[[TIME:.*]] = llvm.getelementptr %[[CONTROL]][0, 0] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(ptr, ptr, i64)>
// CHECK-NEXT: llvm.load %[[TIME]] : !llvm.ptr -> !llvm.ptr
// CHECK: %[[DEADLINE:.*]] = llvm.getelementptr %[[CONTROL]][0, 2] : (!llvm.ptr) -> !llvm.ptr, !llvm.struct<(ptr, ptr, i64)>
// CHECK-NEXT: llvm.load %[[DEADLINE]] {{.*}} : !llvm.ptr -> i64
// CHECK: llvm.load %[[TERMINATION_PTR]] {{.*}} : !llvm.ptr -> i32
// CHECK: llvm.call @obelisk_rt_v1_scheduler_handoff_periodic_aot
