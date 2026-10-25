// RUN: obelisk-opt %s --pass-pipeline='builtin.module(simulation.design(obelisk-sim-build-compute-graph,obelisk-sim-verify-compute-graph,obelisk-sim-materialize-graph-regions,obelisk-sim-materialize-compute-fusion,obelisk-sim-specialize-static-state-nba,obelisk-sim-plan-static-superstep),convert-obelisk-sim-processes-to-llvm-coroutines)' | FileCheck %s

// All selected scalar roots have the same width and storage class. The wide
// root is excluded from the scalar loop and leaves a zero row in its columns;
// folding requires the entire column to agree, including that unselected row.
// The pointer column must retain each scalar accumulator and the null wide row.
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_accumulators_v1() {{.*}} : !llvm.array<10 x ptr>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_fixed_masks_v1(dense<[255, 255, 255, 255, 255, 255, 255, 255, 255, 0]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_regions_v1(dense<[2, 2, 2, 2, 2, 2, 2, 2, 2, 0]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_classes_v1(dense<[8, 8, 8, 8, 8, 8, 8, 8, 8, 0]>
// CHECK-DAG: llvm.mlir.global internal constant @__obelisk_eval_nba_commit_widths_v1(dense<[255, 255, 255, 255, 255, 255, 255, 255, 255, 0]>
// CHECK-LABEL: llvm.func @__obelisk_aot_static_nba_commit_v1(
// CHECK: %[[WIDTHS:.*]] = llvm.mlir.addressof @__obelisk_eval_nba_commit_widths_v1
// CHECK-NEXT: %[[SLOT:.*]] = llvm.getelementptr %[[WIDTHS]][%{{.*}}]
// CHECK-NEXT: llvm.load %[[SLOT]]

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @unselected {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "unselected.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "unselected.writer"
    simulation.storage.decl 0 in 0 : !simulation.logic<1> design
    simulation.storage.decl 1 in 0 : !simulation.logic<8> design
    simulation.storage.decl 2 in 0 : !simulation.logic<8> design
    simulation.storage.decl 3 in 0 : !simulation.logic<8> design
    simulation.storage.decl 4 in 0 : !simulation.logic<8> design
    simulation.storage.decl 5 in 0 : !simulation.logic<8> design
    simulation.storage.decl 6 in 0 : !simulation.logic<8> design
    simulation.storage.decl 7 in 0 : !simulation.logic<8> design
    simulation.storage.decl 8 in 0 : !simulation.logic<8> design
    simulation.storage.decl 9 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 11 in 0 always hierarchy "unselected.watch1"
    simulation.storage.decl 10 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 12 in 0 always hierarchy "unselected.watch2"
    simulation.storage.decl 11 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 13 in 0 always hierarchy "unselected.watch3"
    simulation.storage.decl 12 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 14 in 0 always hierarchy "unselected.watch4"
    simulation.storage.decl 13 in 0 : !simulation.logic<8> design
    simulation.storage.decl 14 in 0 : !simulation.logic<128> design
    simulation.func @root(%ctx: !simulation.context {simulation.capture_kind = 0 : i32})
        attributes {entry_kind = 0 : i32, code_unit_id = 1 : i64} {
      %clk = simulation.context.storage %ctx[0] : !simulation.ref<!simulation.logic<1>>
      %p = simulation.spawn @writer(%ctx, %clk) : !simulation.context, !simulation.ref<!simulation.logic<1>> -> !simulation.process
      %src1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      %w1 = simulation.spawn @watch1(%ctx, %src1) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      %w2 = simulation.spawn @watch2(%ctx, %src2) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src3 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      %w3 = simulation.spawn @watch3(%ctx, %src3) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src4 = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<8>>
      %w4 = simulation.spawn @watch4(%ctx, %src4) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      simulation.return
    }
    simulation.func @writer(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %clk: !simulation.ref<!simulation.logic<1>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 0 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 2 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %clk to ^write {site = #schedule.continuation<id = 1>} : !simulation.ref<!simulation.logic<1>>
    ^write:
      %value = simulation.logic.constant 42 : i8, 0 : i8 : !simulation.logic<8>
      %r1 = simulation.context.storage %ctx[1] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r1 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r2 = simulation.context.storage %ctx[2] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r2 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r3 = simulation.context.storage %ctx[3] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r3 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r4 = simulation.context.storage %ctx[4] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r4 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r5 = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r5 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r6 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r7 = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r7 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r8 = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r8 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %r9 = simulation.context.storage %ctx[9] : !simulation.ref<!simulation.logic<8>>
      simulation.nba.enqueue %value to %r9 : (!simulation.logic<8>, !simulation.ref<!simulation.logic<8>>) -> ()
      %wide = simulation.context.storage %ctx[14] : !simulation.ref<!simulation.logic<128>>
      %wide_value = simulation.logic.constant 17 : i128, 0 : i128 : !simulation.logic<128>
      simulation.nba.enqueue %wide_value to %wide : (!simulation.logic<128>, !simulation.ref<!simulation.logic<128>>) -> ()
      cf.br ^wait
    }
    simulation.func @watch1(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 1 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 11 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 2>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[10] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch2(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 2 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 12 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 3>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[11] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch3(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 3 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 13 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 4>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[12] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch4(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 4 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 14 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 5>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[13] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
  }
}
