// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' -o %t.planned.mlir
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
  simulation.design @optional_dump {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "optional_dump.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "optional_dump.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "optional_dump.consume"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %data = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<32>>
      %index = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %sink = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<8>>
      %flag = simulation.ref.load %index :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %bits = simulation.logic.to_bits %flag : !simulation.logic<64> -> i64
      %zero = arith.constant 0 : i64
      %enabled = arith.cmpi ne, %bits, %zero : i64
      cf.cond_br %enabled, ^dump, ^start
    ^dump:
      %levels = arith.constant 0 : i64
      %scope = simulation.bytes.constant ""
      simulation.dump.vars %ctx, %levels, %scope :
          (!simulation.context, i64, !simulation.bytes) -> ()
      cf.br ^start
    ^start:
      %c = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %p = simulation.spawn @consume(%ctx, %clock, %data, %index, %sink) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<32>>,
          !simulation.ref<!simulation.logic<64>>,
          !simulation.ref<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }

    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
          {site = #schedule.continuation<id = 1>,
           timing = #schedule.timing_site<id = 0, kind = calendar>}
    ^toggle:
      %old = simulation.ref.load %clock :
          !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %new = simulation.logic.unary bit_not %old :
          (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %new to %clock : !simulation.logic<1>,
          !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }

    simulation.func @consume(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clock: !simulation.ref<!simulation.logic<1>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 0 : i64},
        %data: !simulation.ref<!simulation.logic<32>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %index: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %sink: !simulation.ref<!simulation.logic<8>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clock to ^resume
          {site = #schedule.continuation<id = 2>} :
          !simulation.ref<!simulation.logic<1>>
    ^resume:
      %low = simulation.ref.load %index :
          !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      %slice = simulation.ref.dyn_extract %data from %low :
          (!simulation.ref<!simulation.logic<32>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %slice :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.nba.enqueue %value to %sink :
          (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %bits = simulation.logic.to_bits %low : !simulation.logic<64> -> i64
      %zero = arith.constant 0 : i64
      %show = arith.cmpi ne, %bits, %zero : i64
      cf.cond_br %show, ^display, ^wait
    ^display:
      %format = simulation.bytes.constant "value=%h"
      %stdout = arith.constant 1 : i32
      %previous = simulation.ref.load %sink :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.display %ctx to %stdout(%format, %previous)
          newline = true radix = <decimal> flags = [0, 0] :
          !simulation.bytes, !simulation.logic<8>
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
