// RUN: obelisk-opt %s \
// RUN:   --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep))' \
// RUN:   -o %t.planned.mlir
// RUN: obelisk-opt %t.planned.mlir \
// RUN:   --convert-obelisk-sim-processes-to-llvm-coroutines \
// RUN:   | FileCheck %s

// Feed preplanned Simulation IR to one conversion pass.  The emitted eval
// body must keep a dynamic packed load runtime-free while implementing the
// IEEE 1800-2017 11.5.1 partial-overlap rule with signed bounds, a clamped
// physical address, and result masks.  This locks the actual hot-path
// transformation independently of the planning passes above.
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
      simulation.ref.store %value to %sink : !simulation.logic<8>,
          !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
  }
}

// CHECK-LABEL: llvm.func @consume.__obelisk_eval_body_0(
// CHECK: llvm.icmp "sge"
// CHECK: llvm.icmp "sle"
// CHECK: llvm.icmp "slt"
// The upper-bound compare is unsigned after the index has been clamped
// nonnegative; this is equivalent to the source signed comparison.
// CHECK: llvm.icmp "ugt"
// CHECK: llvm.select
// CHECK: llvm.load {{.*}} {alignment = 1 : i64}
// CHECK: llvm.shl
// CHECK: llvm.lshr
// CHECK: llvm.and
// CHECK: llvm.or
// CHECK-NOT: llvm.call @obelisk_rt_v1_native_state_load_plane
// CHECK-NOT: llvm.call @malloc
// CHECK: llvm.return
