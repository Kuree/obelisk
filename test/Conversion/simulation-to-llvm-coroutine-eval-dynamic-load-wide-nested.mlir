// RUN: obelisk-opt %s --split-input-file \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir --split-input-file \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// Variants of simulation-to-llvm-coroutine-eval-dynamic-load. The generated
// eval body reads a dynamic packed selection straight from the canonical
// planes, keeping the IEEE 1800-2023 11.5.1 partial-overlap rule.
//  - wide: a 128-bit window of a 256-bit root is read through an i136 span;
//    the selection is integer arithmetic of the field's own width.
//  - nested: a fixed field of a dynamically indexed unpacked element is an
//    offset of the element's offset. The chain folds to its constant root,
//    and the element's invalid-index guard (7.4.6) still selects the
//    out-of-range value.

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @eval_dynamic_load_wide {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_load_wide.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_load_wide.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_load_wide.consume"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<256> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
    simulation.storage.decl 3 in 0 : !simulation.logic<128> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %data = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.logic<256>>
      %index = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %sink = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<128>>
      %c = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %p = simulation.spawn @consume(%ctx, %clock, %data, %index, %sink) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.logic<256>>,
          !simulation.ref<!simulation.logic<64>>,
          !simulation.ref<!simulation.logic<128>> -> !simulation.process
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
        %data: !simulation.ref<!simulation.logic<256>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 1 : i64},
        %index: !simulation.ref<!simulation.logic<64>>
            {simulation.capture_kind = 3 : i32,
             simulation.descriptor_id = 2 : i64},
        %sink: !simulation.ref<!simulation.logic<128>>
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
          (!simulation.ref<!simulation.logic<256>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<128>>
      %value = simulation.ref.load %slice :
          !simulation.ref<!simulation.logic<128>> -> !simulation.logic<128>
      simulation.ref.store %value to %sink : !simulation.logic<128>,
          !simulation.ref<!simulation.logic<128>>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0(
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i136
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK: llvm.return

// -----

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @eval_dynamic_load_nested {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer
        hierarchy "eval_dynamic_load_nested.root"
    simulation.code_unit.decl 2 in 0 always
        hierarchy "eval_dynamic_load_nested.clock"
    simulation.code_unit.decl 3 in 0 always
        hierarchy "eval_dynamic_load_nested.consume"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.unpacked_array<0 : 3 x !simulation.logic<16>> design
    simulation.storage.decl 2 in 0 : !simulation.logic<64> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design

    simulation.func @root(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clock = simulation.context.storage %ctx[0] :
          !simulation.ref<!simulation.logic<1>>
      %data = simulation.context.storage %ctx[1] :
          !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<16>>>
      %index = simulation.context.storage %ctx[2] :
          !simulation.ref<!simulation.logic<64>>
      %sink = simulation.context.storage %ctx[3] :
          !simulation.ref<!simulation.logic<8>>
      %c = simulation.spawn @clock(%ctx, %clock) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>
          -> !simulation.process
      %p = simulation.spawn @consume(%ctx, %clock, %data, %index, %sink) :
          !simulation.context, !simulation.ref<!simulation.logic<1>>,
          !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<16>>>,
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
        %data: !simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<16>>>
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
      %element = simulation.ref.array_element %data[%low] :
          (!simulation.ref<!simulation.unpacked_array<0 : 3 x !simulation.logic<16>>>,
           !simulation.logic<64>) -> !simulation.ref<!simulation.logic<16>>
      %slice = simulation.ref.extract %element from 8 :
          !simulation.ref<!simulation.logic<16>> -> !simulation.ref<!simulation.logic<8>>
      %value = simulation.ref.load %slice :
          !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      simulation.ref.store %value to %sink : !simulation.logic<8>,
          !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0(
// CHECK: llvm.icmp "sge"
// CHECK: llvm.icmp "sle"
// CHECK: llvm.and
// CHECK: llvm.load {{.*}} : !llvm.ptr -> i16
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_handle_offset
// CHECK: llvm.return
