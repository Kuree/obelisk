// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' -o %t.planned.mlir
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
  simulation.design @eval_dynamic_load {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_load.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_load.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_load.consume"
    simulation.code_unit.decl 4 in 0 function hierarchy "reserved_variant"
    simulation.code_unit.decl 5 in 0 function hierarchy "reserved_path"
    simulation.code_unit.decl 6 in 0 function hierarchy "reserved_checkpoint"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<32> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design

    // Existing declarations must reserve names across variant and predicate
    // creation. These functions are distinct from the generated definitions.
    simulation.func @consume.__obelisk_eval_body_0.__obelisk_two_state_0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 4 : i64} {
      simulation.return
    }
    simulation.func @consume.__obelisk_eval_body_0.__obelisk_path_known_0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 5 : i64} {
      simulation.return
    }
    simulation.func @consume.__obelisk_eval_body_0.__obelisk_checkpoint_path_0(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 8 : i32, code_unit_id = 6 : i64} {
      simulation.return
    }

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
