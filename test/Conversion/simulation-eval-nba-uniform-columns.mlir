// RUN: talos-opt %s --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' -o %t.mlir
// RUN: FileCheck %s --implicit-check-not='@__talos_eval_nba_commit_{{(widths|shifts|classes|regions|fixed_masks|clear|direct)}}_v1' < %t.mlir
// RUN: talos-opt %s --mlir-disable-threading --pass-pipeline='builtin.module(simulation.design(talos-sim-build-compute-graph,talos-sim-verify-compute-graph,talos-sim-materialize-graph-regions,talos-sim-materialize-compute-fusion,talos-sim-specialize-static-state-nba,talos-sim-plan-static-superstep),convert-talos-sim-processes-to-llvm-coroutines)' | diff %t.mlir -
// RUN: talos-opt %t.mlir --canonicalize --cse | FileCheck %s --check-prefix=FOLDED

// Uniform integer columns become constants before native partitioning. This
// includes zero shifts and nonzero width masks, but never the accumulator
// pointer column. The ninth observer has a different trigger and mask; those
// mixed columns must retain their indexed loads. NBA order and four-state
// payloads are unchanged (IEEE 1800-2023 4.3, 4.6, 6.3.1, 10.4.2).
// CHECK-DAG: llvm.mlir.global internal constant @__talos_eval_nba_commit_accumulators_v1() {{.*}} : !llvm.array<9 x ptr>
// CHECK-DAG: llvm.mlir.global internal constant @__talos_eval_nba_commit_edges_v1(dense<[0, 0, 0, 0, 0, 0, 0, 0, 1]>
// CHECK-DAG: llvm.mlir.global internal constant @__talos_eval_nba_commit_watch_masks_v1(dense<[255, 255, 255, 255, 255, 255, 255, 255, 1]>
// CHECK-DAG: llvm.mlir.global internal constant @__talos_eval_nba_commit_transients_v1(dense<[255, 255, 255, 255, 255, 255, 255, 255, 0]>
// CHECK-LABEL: llvm.func @__talos_aot_static_nba_commit_v1(
// CHECK: %[[OFFSETS:.*]] = llvm.mlir.addressof @__talos_eval_nba_commit_offsets_v1
// CHECK-NEXT: %[[SLOT:.*]] = llvm.getelementptr %[[OFFSETS]][%{{.*}}]
// CHECK-NEXT: llvm.load %[[SLOT]]
// CHECK-NEXT: llvm.mlir.constant(0 : i64)
// CHECK-NEXT: llvm.mlir.constant(255 : i64)
// CHECK-NEXT: llvm.mlir.constant(255 : i64)
// CHECK: llvm.mlir.addressof @__talos_eval_nba_commit_edges_v1
// FOLDED-LABEL: llvm.func @__talos_aot_static_nba_commit_v1(
// FOLDED: %[[CLASS:.*]] = llvm.mlir.constant(8 : i64)
// FOLDED: llvm.switch %[[CLASS]] : i64,

module attributes {
  llvm.data_layout = "e-m:e-p:64:64-i64:64-n8:16:32:64-S128",
  llvm.target_triple = "x86_64-unknown-linux-gnu",
  schedule.native_scheduler = 3 : i32
} {
  simulation.design @columns {
    simulation.scope.decl 0
    simulation.code_unit.decl 1 in 0 root_initializer hierarchy "columns.root"
    simulation.code_unit.decl 2 in 0 always hierarchy "columns.writer"
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
    simulation.code_unit.decl 11 in 0 always hierarchy "columns.watch1"
    simulation.storage.decl 10 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 12 in 0 always hierarchy "columns.watch2"
    simulation.storage.decl 11 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 13 in 0 always hierarchy "columns.watch3"
    simulation.storage.decl 12 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 14 in 0 always hierarchy "columns.watch4"
    simulation.storage.decl 13 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 15 in 0 always hierarchy "columns.watch5"
    simulation.storage.decl 14 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 16 in 0 always hierarchy "columns.watch6"
    simulation.storage.decl 15 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 17 in 0 always hierarchy "columns.watch7"
    simulation.storage.decl 16 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 18 in 0 always hierarchy "columns.watch8"
    simulation.storage.decl 17 in 0 : !simulation.logic<8> design
    simulation.code_unit.decl 19 in 0 always hierarchy "columns.watch9"
    simulation.storage.decl 18 in 0 : !simulation.logic<8> design
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
      %src5 = simulation.context.storage %ctx[5] : !simulation.ref<!simulation.logic<8>>
      %w5 = simulation.spawn @watch5(%ctx, %src5) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src6 = simulation.context.storage %ctx[6] : !simulation.ref<!simulation.logic<8>>
      %w6 = simulation.spawn @watch6(%ctx, %src6) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src7 = simulation.context.storage %ctx[7] : !simulation.ref<!simulation.logic<8>>
      %w7 = simulation.spawn @watch7(%ctx, %src7) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src8 = simulation.context.storage %ctx[8] : !simulation.ref<!simulation.logic<8>>
      %w8 = simulation.spawn @watch8(%ctx, %src8) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
      %src9 = simulation.context.storage %ctx[9] : !simulation.ref<!simulation.logic<8>>
      %w9 = simulation.spawn @watch9(%ctx, %src9) : !simulation.context, !simulation.ref<!simulation.logic<8>> -> !simulation.process
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
    simulation.func @watch5(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 5 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 15 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 6>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[14] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch6(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 6 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 16 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 7>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[15] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch7(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 7 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 17 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 8>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[16] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch8(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 8 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 18 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.change %src to ^copy {site = #schedule.continuation<id = 9>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[17] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
    simulation.func @watch9(
        %ctx: !simulation.context {simulation.capture_kind = 0 : i32},
        %src: !simulation.ref<!simulation.logic<8>> {simulation.capture_kind = 3 : i32, simulation.descriptor_id = 9 : i64})
        attributes {entry_kind = 3 : i32, code_unit_id = 19 : i64} {
      cf.br ^wait
    ^wait:
      simulation.suspend.edge posedge %src to ^copy {site = #schedule.continuation<id = 10>} : !simulation.ref<!simulation.logic<8>>
    ^copy:
      %value = simulation.ref.load %src : !simulation.ref<!simulation.logic<8>> -> !simulation.logic<8>
      %dst = simulation.context.storage %ctx[18] : !simulation.ref<!simulation.logic<8>>
      simulation.ref.store %value to %dst : !simulation.logic<8>, !simulation.ref<!simulation.logic<8>>
      cf.br ^wait
    }
  }
}
