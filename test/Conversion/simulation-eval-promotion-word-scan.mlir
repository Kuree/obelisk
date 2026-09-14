// RUN: obelisk-opt %s --pass-pipeline='builtin.module(obelisk_sim.design(obelisk_sim.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s < %t.mlir
// RUN: FileCheck %s --check-prefix=RANGE < %t.mlir
// RUN: %python %S/Inputs/check-promotion-word-scan.py %t.mlir %t mlir-translate %llvm_dist/bin %native_support

// Overlapping views must be scanned as an exact union. Neither partial-byte
// boundary nor the gap to another range may contribute unrelated X/Z bits
// to promotion evidence, regardless of the generated scan's load widths.
// CHECK-LABEL: llvm.func @__obelisk_eval_route_promotion_scan_v1
// CHECK: llvm.load
// CHECK: llvm.icmp "eq"

// RANGE-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_dependencies_v1
// RANGE-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_certificates_v1
// RANGE-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_range_v1
// RANGE: llvm.call @obelisk_rt_v1_native_promotion_invalidate_ranges

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  obelisk.native_scheduler = 3 : i32
} {
  obelisk_sim.design @word_scan {
    obelisk_sim.scope.decl 0
    obelisk_sim.storage.decl 0 in 0 : i1 design
    obelisk_sim.storage.decl 1 in 0 : !obelisk_sim.logic<160> design
    obelisk_sim.storage.decl 2 in 0 : !obelisk_sim.logic<129> design
    obelisk_sim.storage.decl 3 in 0 : !obelisk_sim.logic<64> design
    obelisk_sim.storage.decl 4 in 0 : i1 design
    obelisk_sim.storage.decl 5 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.storage.decl 6 in 0 : !obelisk_sim.logic<16> design
    obelisk_sim.code_unit.decl 4 in 0 always hierarchy "other_clock"
    obelisk_sim.code_unit.decl 5 in 0 always hierarchy "other_work"
    obelisk_sim.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    obelisk_sim.code_unit.decl 2 in 0 always hierarchy "clock"
    obelisk_sim.code_unit.decl 3 in 0 always hierarchy "work"
    obelisk_sim.func @root(%ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = obelisk_sim.context.storage %ctx[0] : !obelisk_sim.ref<i1>
      %c = obelisk_sim.spawn @clock(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %w = obelisk_sim.spawn @work(%ctx, %clk) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %other_clk = obelisk_sim.context.storage %ctx[4] : !obelisk_sim.ref<i1>
      %oc = obelisk_sim.spawn @other_clock(%ctx, %other_clk) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      %ow = obelisk_sim.spawn @other_work(%ctx, %other_clk) : !obelisk_sim.context, !obelisk_sim.ref<i1> -> !obelisk_sim.process
      obelisk_sim.return
    }
    obelisk_sim.func @clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clk : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^body : !obelisk_sim.ref<i1>
    ^body:
      %input = obelisk_sim.context.storage %ctx[1] : !obelisk_sim.ref<!obelisk_sim.logic<160>>
      %output = obelisk_sim.context.storage %ctx[2] : !obelisk_sim.ref<!obelisk_sim.logic<129>>
      %other = obelisk_sim.context.storage %ctx[3] : !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %view = obelisk_sim.ref.extract %input from 3 : !obelisk_sim.ref<!obelisk_sim.logic<160>> -> !obelisk_sim.ref<!obelisk_sim.logic<129>>
      %overlap = obelisk_sim.ref.extract %input from 11 : !obelisk_sim.ref<!obelisk_sim.logic<160>> -> !obelisk_sim.ref<!obelisk_sim.logic<64>>
      %a = obelisk_sim.ref.load %view : !obelisk_sim.ref<!obelisk_sim.logic<129>> -> !obelisk_sim.logic<129>
      %b = obelisk_sim.ref.load %overlap : !obelisk_sim.ref<!obelisk_sim.logic<64>> -> !obelisk_sim.logic<64>
      obelisk_sim.ref.store %a to %output : !obelisk_sim.logic<129>, !obelisk_sim.ref<!obelisk_sim.logic<129>>
      obelisk_sim.ref.store %b to %other : !obelisk_sim.logic<64>, !obelisk_sim.ref<!obelisk_sim.logic<64>>
      cf.br ^wait
    }
    obelisk_sim.func @other_clock(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = obelisk_sim.time.constant 1
      obelisk_sim.suspend.delay %delay to ^toggle
    ^toggle:
      %old = obelisk_sim.ref.load %clk : !obelisk_sim.ref<i1> -> i1
      %one = arith.constant true
      %next = arith.xori %old, %one : i1
      obelisk_sim.ref.store %next to %clk : i1, !obelisk_sim.ref<i1>
      cf.br ^wait
    }
    obelisk_sim.func @other_work(
        %ctx: !obelisk_sim.context {obelisk_sim.capture_kind = 0 : i32},
        %clk: !obelisk_sim.ref<i1> {obelisk_sim.capture_kind = 3 : i32, obelisk_sim.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      obelisk_sim.suspend.change %clk to ^body : !obelisk_sim.ref<i1>
    ^body:
      %input = obelisk_sim.context.storage %ctx[5] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %output = obelisk_sim.context.storage %ctx[6] : !obelisk_sim.ref<!obelisk_sim.logic<16>>
      %value = obelisk_sim.ref.load %input : !obelisk_sim.ref<!obelisk_sim.logic<16>> -> !obelisk_sim.logic<16>
      obelisk_sim.ref.store %value to %output : !obelisk_sim.logic<16>, !obelisk_sim.ref<!obelisk_sim.logic<16>>
      cf.br ^wait
    }
  }
}
