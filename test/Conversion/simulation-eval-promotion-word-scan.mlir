// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: obelisk-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(simulation.func(obelisk-sim-thread-suspension),obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' -o %t.serial
// RUN: diff %t.mlir %t.serial
// RUN: FileCheck %s --implicit-check-not=__obelisk_eval_kernel_promotion_ready_v1_ < %t.mlir
// RUN: FileCheck %s --check-prefix=RANGE < %t.mlir
// RUN: FileCheck %s --check-prefix=KERNEL < %t.mlir
// RUN: FileCheck %s --check-prefix=VARIANT --implicit-check-not=__obelisk_eval_function_route_v1_ --implicit-check-not='llvm.call @__obelisk_eval_variant_dispatch_v1_' < %t.mlir
// RUN: FileCheck %s --check-prefix=STORE < %t.mlir
// RUN: FileCheck %s --check-prefix=CLOCK < %t.mlir

// Runtime behavior is checked in ../Runtime/simulation-eval-promotion-word-scan.test.

// Overlapping views must be scanned as an exact union. Neither partial-byte
// boundary nor the gap to another range may contribute unrelated X/Z bits
// to promotion evidence, regardless of the generated scan's load widths.
// CHECK-LABEL: llvm.func @__obelisk_eval_route_promotion_scan_v1
// CHECK: llvm.load
// CHECK: llvm.icmp "eq"

// KERNEL-DAG: llvm.mlir.global internal constant @__obelisk_eval_kernel_promotion_ranges_v1(dense<[47, 51, 255, 255, 52, 53, 255, 255, 1, 18, 248, 15, 21, 38, 255, 1, 38, 46, 255, 255, 51, 52, 255, 255]>
// KERNEL-DAG: llvm.mlir.global internal constant @__obelisk_eval_kernel_promotion_owners_v1(dense<[0, 2, 0, 1, 2, 6, 0, 2]>
// RANGE-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_dependencies_v1(dense<{{.*}}> : tensor<{{[0-9]+}}x4xi64>)
// RANGE-DAG: llvm.mlir.global internal constant @__obelisk_eval_proof_certificates_v1
// RANGE-LABEL: llvm.func @__obelisk_eval_promotion_invalidate_range_v1
// RANGE: llvm.call @obelisk_rt_v1_native_promotion_invalidate_ranges

// STORE-LABEL: llvm.func @work.__obelisk_eval_body_0(
// STORE: llvm.store
// STORE: llvm.icmp "ne"
// STORE: llvm.cond_br
// STORE: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// STORE-LABEL: llvm.func @other_work.__obelisk_eval_body_0(
// STORE: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// STORE-LABEL: llvm.func @work.__obelisk_eval_body_0.__obelisk_two_state_0(
// STORE-NOT: __obelisk_eval_promotion_publish_unknown_v1
// STORE-LABEL: llvm.func @other_work.__obelisk_eval_body_0.__obelisk_two_state_0(
// STORE-NOT: __obelisk_eval_promotion_publish_unknown_v1
// STORE: llvm.func

// Four-state clocks have real unknown-plane accesses, but their bytes are
// outside every value-domain certificate. Keep their canonical semantics
// without emitting guards or proof publications in the generated clock loop.
// CLOCK-LABEL: llvm.func @__obelisk_aot_schedule_run_v1(
// CLOCK-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// CLOCK: llvm.mlir.addressof @__obelisk_state_unknown
// CLOCK-NOT: llvm.call @__obelisk_eval_promotion_publish_unknown_v1
// CLOCK: llvm.func

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @word_scan {
    simulation.scope.decl 0
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<160> design
    simulation.storage.decl 2 in 0 : !simulation.logic<129> design
    simulation.storage.decl 3 in 0 : !simulation.logic<64> design
    simulation.storage.decl 4 in 0 : !simulation.logic<1> design
    simulation.storage.decl 5 in 0 : !simulation.logic<16> design
    simulation.storage.decl 6 in 0 : !simulation.logic<16> design
    simulation.storage.decl 7 in 0 : !simulation.logic<8> design
    simulation.storage.decl 8 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 4 in 0 always hierarchy "other_clock"
    simulation.code_unit.decl 5 in 0 always hierarchy "other_work"
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "root"
    simulation.code_unit.decl 2 in 0 always hierarchy "clock"
    simulation.code_unit.decl 3 in 0 always hierarchy "work"
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %c = simulation.spawn @clock(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %w = simulation.spawn @work(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %other_clk = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<1>>
      %oc = simulation.spawn @other_clock(%ctx, %other_clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %ow = simulation.spawn @other_work(%ctx, %other_clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      simulation.return
    }
    simulation.func @clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 3 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %input = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<160>>
      %output = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<129>>
      %other = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<64>>
      %view = simulation.ref.extract %input from 3 : !simulation.ref<!simulation.logic<160>> -> !simulation.ref<!simulation.logic<129>>
      %overlap = simulation.ref.extract %input from 11 : !simulation.ref<!simulation.logic<160>> -> !simulation.ref<!simulation.logic<64>>
      %a = simulation.ref.load %view : !simulation.ref<!simulation.logic<129>> -> !simulation.logic<129>
      %b = simulation.ref.load %overlap : !simulation.ref<!simulation.logic<64>> -> !simulation.logic<64>
      simulation.ref.store %a to %output : !simulation.logic<129>, !simulation.ref<!simulation.logic<129>>
      simulation.ref.store %b to %other : !simulation.logic<64>, !simulation.ref<!simulation.logic<64>>
      %nba = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<8>>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %zero to %nba : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
    simulation.func @other_clock(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 4 : i64} {
      cf.br ^wait
    ^wait:
      %delay = simulation.time.constant 1
      simulation.suspend.delay %delay to ^toggle
    ^toggle:
      %old = simulation.ref.load %clk : !simulation.ref<!simulation.logic<1>> -> !simulation.logic<1>
      %next = simulation.logic.unary bit_not %old : (!simulation.logic<1>) -> !simulation.logic<1>
      simulation.ref.store %next to %clk : !simulation.logic<1>, !simulation.ref<!simulation.logic<1>>
      cf.br ^wait
    }
    simulation.func @other_work(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 5 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %clk to ^body : !simulation.ref<!simulation.logic<1>>
    ^body:
      %input = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<16>>
      %output = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<16>>
      %value = simulation.ref.load %input : !simulation.ref<!simulation.logic<16>> -> !simulation.logic<16>
      simulation.ref.store %value to %output : !simulation.logic<16>, !simulation.ref<!simulation.logic<16>>
      %nba = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<8>>
      %zero = simulation.logic.constant 0 : i8, 0 : i8 : !simulation.logic<8>
      simulation.nba.enqueue %zero to %nba : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      cf.br ^wait
    }
  }
}

// VARIANT: llvm.mlir.global internal @__obelisk_eval_selected_variant_v1_0(0 : i8)
// VARIANT-LABEL: llvm.func internal @__obelisk_eval_variant_dispatch_v1_0(
// VARIANT-SAME: always_inline
// VARIANT: llvm.mlir.addressof @__obelisk_eval_selected_variant_v1_0
// VARIANT: llvm.load {{.*}} : !llvm.ptr -> i8
// VARIANT: llvm.cond_br
// VARIANT: llvm.call @work.__obelisk_eval_body_0.__obelisk_two_state_0
// VARIANT: llvm.call @__obelisk_eval_four_state_fallback_v1_0
