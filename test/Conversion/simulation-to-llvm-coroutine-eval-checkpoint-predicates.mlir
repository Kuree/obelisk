// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir --convert-obelisk-sim-processes-to-llvm-coroutines -o %t.llvm.mlir
// RUN: FileCheck %s < %t.llvm.mlir

// The coroutine conversion must rewrite both checkpoint predicates as part
// of the generated closure, including dead dynamic-handle computations.
// The cold callback must publish its committed planes before returning to
// the runtime, which otherwise exports the pre-checkpoint state over them.
module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  obelisk_sim.design @eval_dynamic_load {
    obelisk_sim.scope.decl 0
    obelisk_sim.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_load.root"
    obelisk_sim.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_load.clock"
    obelisk_sim.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_load.consume"
    obelisk_sim.code_unit.decl 4 in 0 function hierarchy "reserved_variant"
    obelisk_sim.code_unit.decl 5 in 0 function hierarchy "reserved_path"
    obelisk_sim.code_unit.decl 6 in 0 function hierarchy "reserved_checkpoint"
    obelisk_sim.storage.decl 0 in 0 : !obelisk_sim.logic<1> design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<32> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<64> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<8> design

    // Existing declarations must reserve names across variant and predicate
    // creation. These functions are distinct from the generated definitions.
    obelisk_sim.func @consume.__obelisk_eval_body_0.__obelisk_two_state_0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @consume.__obelisk_eval_body_0.__obelisk_path_known_0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      obelisk_sim.return
    }
    obelisk_sim.func @consume.__obelisk_eval_body_0.__obelisk_checkpoint_path_0(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      obelisk_sim.return
    }

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

// CHECK-DAG: llvm.func @consume.__obelisk_eval_body_0.__obelisk_two_state_0(
// CHECK-DAG: llvm.func @consume.__obelisk_eval_body_0.__obelisk_path_known_0(
// CHECK-DAG: llvm.func @consume.__obelisk_eval_body_0.__obelisk_checkpoint_path_0(
// CHECK-DAG: llvm.func @consume.__obelisk_eval_body_0.__obelisk_two_state_1(
// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0.__obelisk_path_known_1(
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return
// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0.__obelisk_checkpoint_path_1(
// CHECK-NOT: llvm.call @obelisk_rt_
// CHECK: llvm.return
// CHECK: llvm.func @__obelisk_eval_four_state_fallback_v1_
// CHECK: llvm.call @__obelisk_eval_checkpoint_body_v1_
// CHECK: llvm.call @__obelisk_eval_dispatch_v1
// CHECK: llvm.call @obelisk_rt_v1_native_state_sync
